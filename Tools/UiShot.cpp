// Renders the MIDIARCHIV editor into PNG files (UI checks without a DAW) and writes the icon.
// Usage: UiShot icon <dir>
//        UiShot shot <out.png> [width height] [select <row>] [about|tips] [sort <col>] [dupsonly] [light] [theme n] [menu]
// Set MIDIARCHIV_DB to use a copy of a database.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/IconArt.h"

class UiShotApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "UiShot"; }
    const juce::String getApplicationVersion() override { return "1"; }

    static void writePng (const juce::Image& img, const juce::File& out)
    {
        out.deleteFile();
        juce::FileOutputStream os (out);
        juce::PNGImageFormat().writeImageToStream (img, os);
    }

    void initialise (const juce::String&) override
    {
        const auto args = getCommandLineParameterArray();
        if (args.size() >= 2 && args[0] == "icon")
        {
            juce::File dir (args[1]);
            dir.createDirectory();
            for (int s : { 16, 24, 32, 48, 64, 128, 256, 512, 1024 })
            {
                juce::Image img (juce::Image::ARGB, s, s, true, juce::SoftwareImageType());
                {
                    juce::Graphics g (img);
                    ma::drawLogo (g, { 0.0f, 0.0f, (float) s, (float) s });
                }
                writePng (img, dir.getChildFile ("icon_" + juce::String (s) + ".png"));
            }
            quit();
            return;
        }
        if (args.size() >= 3 && args[0] == "synctest")   // UiShot synctest <testfile.mid> <out.txt>: DAW Play/Stop, position and tempo
        {
            juce::String r;
            int fails = 0;
            auto check = [&] (bool ok, const juce::String& what) { r << (ok ? "ok:   " : "FAIL: ") << what << "\n"; if (! ok) ++fails; };
            // test file: 480 PPQ, 120 BPM, one note per beat, note number = beat index
            juce::MidiMessageSequence tr;
            tr.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0);
            for (int b = 0; b < 100; ++b)
            {
                tr.addEvent (juce::MidiMessage::noteOn (1, b, (juce::uint8) 100), b * 480);
                tr.addEvent (juce::MidiMessage::noteOff (1, b), b * 480 + 240);
            }
            juce::MidiFile mf;
            mf.setTicksPerQuarterNote (480);
            mf.addTrack (tr);
            const juce::File cue (args[1]);
            cue.deleteFile();
            { juce::FileOutputStream os (cue); mf.writeTo (os); }

            struct FakeHead : juce::AudioPlayHead
            {
                bool playing = false; double ppq = 0, bpm = 120;
                juce::Optional<PositionInfo> getPosition() const override
                { PositionInfo p; p.setIsPlaying (playing); p.setPpqPosition (ppq); p.setBpm (bpm); return p; }
            } head;
            for (const bool tempo : { true, false })
            {
                MidiArchivProcessor proc;
                const bool oldSync = proc.hostSync(), oldTempo = proc.hostTempo();
                proc.setPlayConfigDetails (0, 2, 48000.0, 512);
                proc.prepareToPlay (48000.0, 512);
                proc.setSoundMode (MidiArchivProcessor::sndNone);
                proc.setPlayHead (&head);
                proc.setHostSync (true);
                proc.setHostTempo (tempo);
                head = {};
                proc.setCue (cue.getFullPathName());   // = selecting the file in the table
                juce::AudioBuffer<float> buf (2, 512);
                juce::MidiBuffer mb;
                juce::Array<int> notes;
                auto run = [&] (double seconds)
                {
                    notes.clear();
                    for (int i = 0, n = juce::roundToInt (seconds * 48000.0 / 512.0); i < n; ++i)
                    {
                        mb.clear();
                        proc.processBlock (buf, mb);
                        for (const auto md : mb) if (md.getMessage().isNoteOn()) notes.add (md.getMessage().getNoteNumber());
                        if (head.playing) head.ppq += 512.0 / 48000.0 * head.bpm / 60.0;
                    }
                };
                auto list = [&] { juce::StringArray a; for (int n : notes) a.add (juce::String (n)); return a.joinIntoString (" "); };
                const juce::String mode = tempo ? "[DAW tempo] " : "[own tempo] ";
                run (0.3);
                check (notes.isEmpty(), mode + "silent while the DAW is stopped");
                head.ppq = 60.0; head.bpm = 60.0; head.playing = true;   // Play at bar 16 (4/4), DAW at 60 BPM
                run (4.4);
                if (tempo) check (notes == juce::Array<int> ({ 60, 61, 62, 63, 64 }), mode + "Play at bar 16, 60 BPM -> beats 60..64: " + list());
                else       check (notes == juce::Array<int> ({ 60, 61, 62, 63, 64, 65, 66, 67, 68 }), mode + "Play at bar 16 -> file's 120 BPM, beats 60..68: " + list());
                head.ppq = 8.0;   // DAW loop / jump back
                run (2.2);
                if (tempo) check (notes == juce::Array<int> ({ 8, 9, 10 }), mode + "jump to beat 8: " + list());
                else       check (notes.size() >= 4 && notes[0] == 8 && notes[1] == 9, mode + "jump to beat 8: " + list());
                head.bpm = 240.0;
                run (1.1);
                if (tempo) check (notes.size() >= 4 && notes.size() <= 5, mode + "tempo change to 240 BPM: " + list());
                head.playing = false;
                run (0.1);
                run (1.0);
                check (notes.isEmpty() && ! proc.isPlaying(), mode + "Stop -> silent");
                head.playing = true; head.ppq = 30.0; head.bpm = 120.0;
                run (0.6);
                check (notes.size() >= 1 && notes[0] == 30, mode + "Play again at beat 30: " + list());
                proc.setPlayHead (nullptr);
                proc.setHostSync (oldSync);
                proc.setHostTempo (oldTempo);
            }
            r << (fails == 0 ? "ALL OK" : "FAILURES") << "\n";
            juce::File (args[2]).replaceWithText (r);
            quit();
            return;
        }
        if (args.size() >= 4 && args[0] == "carrytest")   // UiShot carrytest <a.mid> <b.mid> <out.txt>: does file A (pedal left down) affect file B?
        {
            MidiArchivProcessor proc;
            proc.setPlayConfigDetails (0, 2, 48000.0, 512);
            proc.prepareToPlay (48000.0, 512);
            const auto t0 = juce::Time::getMillisecondCounter();
            while (! proc.soundFont().isLoaded() && juce::Time::getMillisecondCounter() - t0 < 10000) juce::Thread::sleep (20);
            proc.setSoundMode (MidiArchivProcessor::sndSoundFont);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer mb;
            juce::String r;
            proc.play (juce::File (args[1]));
            for (int i = 0; i < 48000 * 2 / 512; ++i) proc.processBlock (buf, mb);
            r << "after A: voices=" << proc.soundFont().activeVoices() << juce::newLine;
            proc.play (juce::File (args[2]));
            int maxV = 0;
            for (int i = 0; i < 48000 * 30 / 512; ++i)
            {
                proc.processBlock (buf, mb);
                maxV = juce::jmax (maxV, proc.soundFont().activeVoices());
                if ((i + 1) % (48000 * 10 / 512) == 0) { r << "B " << (i + 1) * 512 / 48000 << "s: voices=" << proc.soundFont().activeVoices() << " max=" << maxV << juce::newLine; maxV = 0; }
            }
            juce::File (args[3]).replaceWithText (r);
            quit();
            return;
        }
        if (args.size() >= 3 && args[0] == "voicetest")   // UiShot voicetest <mid> <out.txt>: SoundFont voices per second over the whole file
        {
            const int sr = args.size() > 3 ? args[3].getIntValue() : 48000, bs = args.size() > 4 ? args[4].getIntValue() : 512;
            MidiArchivProcessor proc;
            proc.setPlayConfigDetails (0, 2, (double) sr, bs);
            proc.prepareToPlay ((double) sr, bs);
            const auto t0 = juce::Time::getMillisecondCounter();
            while (! proc.soundFont().isLoaded() && juce::Time::getMillisecondCounter() - t0 < 10000) juce::Thread::sleep (20);
            proc.setSoundMode (MidiArchivProcessor::sndSoundFont);
            proc.play (juce::File (args[1]));
            juce::AudioBuffer<float> buf (2, bs);
            juce::MidiBuffer mb;
            juce::String r;
            int ons = 0, offs = 0, maxV = 0;
            float peak = 0;
            double sumSq = 0;
            juce::AudioBuffer<float> all (2, sr * 120);
            int written = 0;
            for (int i = 0; proc.isPlaying() && i < sr * 600 / bs; ++i)
            {
                proc.processBlock (buf, mb);
                for (const auto md : mb) { if (md.getMessage().isNoteOn()) ++ons; else if (md.getMessage().isNoteOff()) ++offs; }
                maxV = juce::jmax (maxV, proc.soundFont().activeVoices());
                peak = juce::jmax (peak, buf.getMagnitude (0, bs));
                for (int k = 0; k < bs; ++k) sumSq += buf.getSample (0, k) * buf.getSample (0, k);
                if (written + bs <= all.getNumSamples()) { for (int c = 0; c < 2; ++c) all.copyFrom (c, written, buf, c, 0, bs); written += bs; }
                if ((i + 1) % (sr * 5 / bs) == 0)
                {
                    r << juce::String ((i + 1) * bs / sr) << "s voices=" << proc.soundFont().activeVoices() << " max=" << maxV << " peak=" << peak << " ons=" << ons << " offs=" << offs << " rms=" << std::sqrt (sumSq / ((double) sr * 5.0)) << juce::newLine;
                    maxV = 0; peak = 0; sumSq = 0;
                }
            }
            juce::File (args[2]).replaceWithText (r);
            const auto wav = juce::File (args[2]).withFileExtension ("wav");
            wav.deleteFile();
            if (auto os = wav.createOutputStream())   // plain 16-bit stereo WAV
            {
                const int bytes = written * 4;
                os->write ("RIFF", 4); os->writeInt (36 + bytes); os->write ("WAVEfmt ", 8);
                os->writeInt (16); os->writeShort (1); os->writeShort (2); os->writeInt (sr); os->writeInt (sr * 4); os->writeShort (4); os->writeShort (16);
                os->write ("data", 4); os->writeInt (bytes);
                for (int k = 0; k < written; ++k)
                    for (int c = 0; c < 2; ++c) os->writeShort ((short) juce::roundToInt (juce::jlimit (-1.0f, 1.0f, all.getSample (c, k)) * 32767.0f));
            }
            quit();
            return;
        }
        if (args.size() >= 3 && args[0] == "playtest")
        {
            MidiArchivProcessor proc;
            proc.setPlayConfigDetails (0, 2, 48000.0, 512);
            proc.prepareToPlay (48000.0, 512);
            const auto t0 = juce::Time::getMillisecondCounter();
            while (! proc.soundFont().isLoaded() && juce::Time::getMillisecondCounter() - t0 < 10000) juce::Thread::sleep (20);
            const auto loadMs = juce::Time::getMillisecondCounter() - t0;
            if (args.size() > 3) proc.setSoundMode (args[3].getIntValue());
            const bool ok = proc.play (juce::File (args[1]));
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer mb;
            int events = 0; float peak = 0;
            for (int i = 0; i < 48000 * 8 / 512; ++i) { proc.processBlock (buf, mb); events += mb.getNumEvents(); peak = juce::jmax (peak, buf.getMagnitude (0, 512)); }
            proc.stop();
            proc.processBlock (buf, mb);
            juce::String r;
            r << "sf=" << proc.soundFont().file().getFileName() << " loadMs=" << (int) loadMs << " mode=" << proc.soundMode() << " " << proc.soundFontError() << juce::newLine;
            r << "play=" << (int) ok << " midiEvents=" << events << " peak=" << peak << " pos=" << proc.position() << " len=" << proc.length()
              << " allOffAfterStop=" << mb.getNumEvents();
            auto info = ma::analyseMidiFile (juce::File (args[1]), true);
            auto mel = ma::extractMelody (info.notes, 16);
            juce::StringArray names;
            for (auto& n : mel) names.add (ma::noteName (n.pitch));
            r << juce::newLine << "melody=" << names.joinIntoString (" ") << "  " << ma::parsonsCode (mel);
            juce::File (args[2]).replaceWithText (r);
            quit();
            return;
        }
        if (args.size() < 2 || args[0] != "shot") { quit(); return; }
        const juce::File out (args[1]);
        int w = 1440, h = 880;
        if (args.size() > 3 && args[2].containsOnly ("0123456789")) { w = args[2].getIntValue(); h = args[3].getIntValue(); }
        MidiArchivProcessor proc;
        proc.uiState.setProperty ("w", w, nullptr);
        proc.uiState.setProperty ("h", h, nullptr);
        {
            MidiArchivEditor ed (proc);
            out.withFileExtension ("txt").replaceWithText ("startSize=" + juce::String (ed.getWidth()) + "x" + juce::String (ed.getHeight())
                + " selected=" + juce::String (ed.tableView().selectedEntry() != nullptr ? ed.tableView().selectedEntry()->id : 0)
                + " rows=" + juce::String (ed.tableView().numRows()));
            if (args.contains ("keepsize")) { w = ed.getWidth(); h = ed.getHeight(); }
            ed.setSize (w, h);
            auto& arch = ed.archive();
            arch.waitForIndex();
            if (int i = args.indexOf ("light"); i > 0) { arch.settings.setProperty ("light", true, nullptr); ed.applyTheme(); }
            if (int i = args.indexOf ("theme"); i > 0) { arch.settings.setProperty ("theme", args[i + 1].getIntValue(), nullptr); ed.applyTheme(); }
            if (int i = args.indexOf ("sort"); i > 0) ed.tableView().list().getHeader().setSortColumnId (args[i + 1].getIntValue(), true);
            if (args.contains ("dupsonly")) { ed.tableView().onlyDuplicates = true; ed.tableView().list().getHeader().setSortColumnId (ma::cDup, true); }
            if (int i = args.indexOf ("filter"); i > 0) ed.tableView().footer->setFilter (args[i + 1].getIntValue(), args[i + 2]);
            ed.refreshNow();
            if (int i = args.indexOf ("select"); i > 0) ed.tableView().list().selectRow (args[i + 1].getIntValue());
            if (args.contains ("about")) ed.showAbout (true);
            if (int i = args.indexOf ("protect"); i > 0 && args[i + 1].getIntValue() < arch.folders.size()) { arch.setProtected (arch.folders[args[i + 1].getIntValue()], true); ed.refreshNow(); }
            if (args.contains ("manage")) ed.scanAddedFolders ({}, true);
            if (args.contains ("folderpick") && arch.folders.size() > 0) ed.scanAddedFolders ({ arch.folders[arch.folders.size() - 1] });   // picker only, scan starts on click
            if (args.contains ("edit")) ed.tableView().startEdit (ed.tableView().list().getSelectedRow(), ma::cTitle);
            if (args.contains ("keys"))   // cell navigation without writing anything (Return on an unchanged value writes nothing)
            {
                auto& tv = ed.tableView();
                juce::String log;
                auto state = [&] (const juce::String& step)
                {
                    auto* ce = tv.cellEditor();
                    log << step << ": row=" << tv.list().getSelectedRow() << " col=" << tv.cursorColumn()
                        << " editing=" << (int) (ce != nullptr);
                    if (ce != nullptr) log << " text=\"" << ce->getText() << "\" allSelected=" << (int) (ce->getHighlightedRegion().getLength() == ce->getTotalNumChars());
                    log << "\n";
                };
                auto tableKey = [&] (int code, juce::ModifierKeys m = {}) { if (! tv.list().keyPressed (juce::KeyPress (code, m, 0))) tv.keyPressed (juce::KeyPress (code, m, 0)); };
                auto edKey = [&] (int code) { if (auto* ce = tv.cellEditor()) ce->keyPressed (juce::KeyPress (code, {}, 0)); };
                state ("start");
                // scrolling while a cell is open: the editor must stay on its row (not float at a fixed spot)
                {
                    const int r0 = tv.list().getSelectedRow();
                    tv.startEdit (r0, ma::cTitle);
                    if (auto* ce = tv.cellEditor())
                    {
                        auto* vp = tv.list().getViewport();
                        auto rowTop = [&] { return tv.list().getLocalArea (ce->getParentComponent(), ce->getBounds()).getY() - tv.list().getCellPosition (ma::cTitle, r0, true).getY(); };
                        const int before = rowTop();
                        vp->setViewPosition (vp->getViewPositionX(), vp->getViewPositionY() + 5 * tv.list().getRowHeight());
                        log << "scroll: viewY=" << vp->getViewPositionY() << " offset before=" << before << " after=" << rowTop() << " followsRow=" << (int) (before == rowTop()) << "\n";
                        vp->setViewPosition (vp->getViewPositionX(), vp->getViewPositionY() - 5 * tv.list().getRowHeight());
                        ce->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey, {}, 0));
                    }
                }
                tableKey (juce::KeyPress::rightKey); state ("right");
                tableKey (juce::KeyPress::rightKey); state ("right");
                tableKey (juce::KeyPress::leftKey);  state ("left");
                tableKey (juce::KeyPress::leftKey);  state ("left");
                tableKey (juce::KeyPress::downKey);  state ("down (table)");
                tableKey (juce::KeyPress::returnKey); state ("return (table)");
                edKey (juce::KeyPress::returnKey);   state ("return (editor)");
                edKey (juce::KeyPress::returnKey);   state ("return (editor)");
                edKey (juce::KeyPress::upKey);       state ("up (editor)");
                edKey (juce::KeyPress::rightKey);    state ("right (editor, all selected)");
                edKey (juce::KeyPress::leftKey);     state ("left (editor, all selected)");
                edKey (juce::KeyPress::tabKey);      state ("tab (editor)");
                if (auto* ce = tv.cellEditor()) ce->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey, {}, 0));
                state ("escape");
                tv.keyPressed (juce::KeyPress ('x', {}, 'x')); state ("type x");
                if (auto* ce = tv.cellEditor()) ce->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey, {}, 0));
                state ("escape");
                // undo: type into a field, Return writes the file, undo brings back the exact bytes
                if (auto* en = tv.selectedEntry())
                {
                    const juce::File f (en->path);
                    juce::MemoryBlock before, after, undone;
                    f.loadFileAsData (before);
                    const int rating0 = en->rating;
                    tv.keyPressed (juce::KeyPress ('Q', {}, 'Q')); state ("type Q");
                    edKey (juce::KeyPress::leftKey);   state ("left (editor, typing)");
                    edKey (juce::KeyPress::returnKey); state ("return (editor) writes");
                    f.loadFileAsData (after);
                    {
                        ma::Archive::UndoGroup g (ed.archive(), "rating");
                        ed.archive().setRating (*en, rating0 == 5 ? 1 : 5);
                    }
                    log << "canUndo=" << (int) ed.archive().canUndo() << " label=" << ed.archive().undoLabel() << "\n";
                    ed.undo();
                    log << "rating restored=" << (int) (en->rating == rating0) << " label now=" << ed.archive().undoLabel() << "\n";
                    ed.undo();
                    f.loadFileAsData (undone);
                    log << "file changed by edit=" << (int) (after != before) << " restored by undo=" << (int) (undone == before)
                        << " canUndo=" << (int) ed.archive().canUndo() << "\n";
                }
                out.withFileExtension ("keys.txt").replaceWithText (log);
            }
            if (! args.contains ("clean")) ed.setStatus ("UiShot");   // "clean" = screenshots for docs/web
            writePng (ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f), out);
        }
        quit();
    }
    void shutdown() override {}
};

START_JUCE_APPLICATION (UiShotApp)
