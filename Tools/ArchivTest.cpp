// MIDIARCHIV console tests.
//   ArchivTest info <file>                 – analysis of one file
//   ArchivTest write <file>                – writes all fields into a temp copy, checks read-back and unchanged notes
//   ArchivTest scan <db> <folder> [...]    – scans folders into <db>, prints timing and duplicate groups
//   ArchivTest groups <db> [n]             – prints the first n duplicate groups of an existing db
#include "../Source/Columns.h"
#include "../Source/UltraStar.h"
#include "../Source/Formats.h"
#include "../Source/Export.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Source/WebLookup.h"
#include <iostream>
#include <map>
#include <set>

static void out (const juce::String& s) { std::cout << s.toRawUTF8() << std::endl; }
static int failures = 0;
static void check (bool ok, const juce::String& what) { out ((ok ? "ok:   " : "FAIL: ") + what); if (! ok) ++failures; }

static void printInfo (const ma::MidiInfo& m)
{
    out ("ok=" + juce::String ((int) m.ok) + " err=" + m.error + " format=" + juce::String (m.format) + " tracks=" + juce::String (m.numTracks) + " ppq=" + juce::String (m.ppq));
    out ("title=" + m.title + " | artist=" + m.artist + " | composer=" + m.composer + " | genre=" + m.genre + " | year=" + juce::String (m.year));
    out ("copyright=" + m.copyright + " | language=" + m.language + " | karaoke=" + juce::String ((int) m.karaoke) + " lyrics=" + juce::String (m.lyrics.length()));
    out ("comment=" + m.comment.substring (0, 200));
    out ("key=" + m.key + " (" + juce::String (m.keyConfidence, 2) + ") meta=" + m.keyMeta + " scale=" + m.scale + " bpm=" + juce::String (m.bpm, 1)
         + " sig=" + m.timeSig + " dur=" + ma::formatDuration (m.seconds));
    out ("notes=" + juce::String (m.noteCount) + " drums=" + juce::String (m.drumNotes) + " channels=" + juce::String (m.channelCount) + " instr=" + m.instruments);
    out ("grams=" + juce::String (m.gramCount) + " sketch=" + juce::String ((int) m.sketch.size()) + " noteHash=" + juce::String::toHexString ((juce::int64) m.noteHash));
    out ("tracks: " + m.trackNames.joinIntoString (" / "));
}

static int writeTest (const juce::File& src)
{
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ma_writetest" + src.getFileExtension());
    src.copyFileTo (tmp);
    const auto before = ma::analyseMidiFile (tmp);
    check (before.ok, "parse original");
    struct T { ma::Field f; juce::String v; };
    const T tests[] = { { ma::Field::title, juce::String::fromUTF8 ("Testtitel Ä") }, { ma::Field::artist, "Test Artist" }, { ma::Field::composer, "J. S. Bach" },
                        { ma::Field::genre, "Klassik" }, { ma::Field::year, "1723" }, { ma::Field::copyright, "(C) Test" },
                        { ma::Field::comment, "Kommentar 1" }, { ma::Field::language, "Deutsch" }, { ma::Field::keySignature, "fis-Moll" } };
    for (auto& t : tests)
    {
        juce::String err;
        const bool ok = ma::writeMidiField (tmp, t.f, t.v, err);
        check (ok, "write field " + juce::String ((int) t.f) + " " + err);
    }
    auto a = ma::analyseMidiFile (tmp);
    check (a.title == tests[0].v, "title read back: " + a.title);
    check (a.artist == "Test Artist", "artist read back: " + a.artist);
    check (a.composer == "J. S. Bach", "composer read back: " + a.composer);
    check (a.genre == "Klassik", "genre read back");
    check (a.year == 1723, "year read back");
    check (a.copyright == "(C) Test", "copyright read back");
    check (a.comment == "Kommentar 1", "comment read back");
    check (a.language == "Deutsch", "language read back");
    check (a.keyMeta == "fis-Moll", "key signature read back: " + a.keyMeta);
    {
        int t; bool mi;
        check (ma::parseKeyName ("Es-Dur", t, mi) && t == 3 && ! mi, "parse Es-Dur");
        check (ma::parseKeyName ("F#m", t, mi) && t == 6 && mi, "parse F#m");
        check (ma::parseKeyName ("b", t, mi) && t == 10 && mi, "parse b = b-Moll");
        check (ma::parseKeyName ("H", t, mi) && t == 11 && ! mi, "parse H");
        check (ma::parseKeyName ("as-moll", t, mi) && t == 8 && mi, "parse as-moll");
        check (! ma::parseKeyName ("xyz", t, mi), "reject xyz");
    }
    check (a.noteHash == before.noteHash && a.noteCount == before.noteCount, "notes unchanged");
    // overwrite and remove
    juce::String err;
    ma::writeMidiField (tmp, ma::Field::artist, "Second", err);
    ma::writeMidiField (tmp, ma::Field::genre, "", err);
    auto b = ma::analyseMidiFile (tmp);
    check (b.artist == "Second", "artist overwritten (no duplicate tag)");
    check (b.genre.isEmpty(), "genre removed");
    // JUCE reads it too
    {
        juce::MemoryBlock mb, smf; size_t off = 0;
        tmp.loadFileAsData (mb);
        ma::extractSmf (mb, smf, off);
        juce::MemoryInputStream is (smf, false);
        juce::MidiFile mf;
        check (mf.readFrom (is), "juce::MidiFile reads the rewritten file");
    }
    tmp.deleteFile();
    return failures;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    _putenv_s ("MIDIARCHIV_BACKUP", juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ma_test_backup").getFullPathName().toRawUTF8());
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (juce::String::fromUTF8 (argv[i]));
    if (args.size() >= 2 && args[0] == "info") { printInfo (ma::analyseMidiFile (juce::File (args[1]))); return 0; }
    if (args.size() >= 2 && args[0] == "lookup")
    {
        juce::String log;
        auto res = ma::lookupOnline ({ args[1], args.size() > 2 ? args[2] : juce::String(), {} }, log);
        out (log);
        for (auto& r : res) out (juce::String ((int) r.field) + ": " + r.value + "   <- " + r.source);
        return 0;
    }
    if (args.size() >= 2 && args[0] == "fileops")
    {
        auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ma_fileops");
        root.deleteRecursively();
        auto a = root.getChildFile ("A"), b = root.getChildFile ("B");
        a.createDirectory(); b.createDirectory();
        juce::File (args[1]).copyFileTo (a.getChildFile ("one.mid"));
        juce::File (args[1]).copyFileTo (a.getChildFile ("two.mid"));
        _putenv_s ("MIDIARCHIV_DB", root.getChildFile ("t.mdb").getFullPathName().toRawUTF8());
        ma::Archive arch;
        arch.waitForIndex();
        arch.addFolder (a);
        arch.startScan();
        arch.waitForScan();
        check (arch.entries().size() == 2, "scan finds 2");
        check (arch.duplicateGroups() == 1, "identical copies form one group");
        auto* one = arch.byPath (a.getChildFile ("one.mid").getFullPathName());
        one->rating = 4;
        const int id = one->id;
        // move to B
        auto dst = b.getChildFile ("one.mid");
        check (a.getChildFile ("one.mid").moveFileTo (dst), "move file");
        arch.relocate (*one, dst);
        check (arch.byPath (dst.getFullPathName()) == one && one->id == id && one->rating == 4, "relocate keeps id and rating");
        check (arch.byPath (a.getChildFile ("one.mid").getFullPathName()) == nullptr, "old path gone");
        // copy into archive folder
        auto copy = a.getChildFile ("three.mid");
        dst.copyFileTo (copy);
        check (arch.addFile (copy) != nullptr && arch.entries().size() == 3, "addFile adds a copy");
        check (arch.byPath (copy.getFullPathName())->groupSize == 3, "copy joins the duplicate group");
        // delete
        arch.removeEntries ({ id });
        check (arch.entries().size() == 2 && arch.byId (id) == nullptr, "removeEntries");
        check (arch.byPath (copy.getFullPathName())->groupSize == 2, "group shrinks after removal");
        check (arch.isInsideFolders (copy) && ! arch.isInsideFolders (dst), "isInsideFolders");
        root.deleteRecursively();
        return failures;
    }
    if (args.size() >= 2 && args[0] == "write") return writeTest (juce::File (args[1]));
    if (args.size() >= 3 && args[0] == "batch")   // ArchivTest batch <list.txt (UTF-8 paths)> <out.txt>: ok|seconds|notes|format|error|path
    {
        juce::StringArray lines, outLines;
        lines.addLines (juce::File (args[1]).loadFileAsString());
        for (auto& l : lines)
        {
            if (l.trim().isEmpty()) continue;
            const auto m = ma::analyseMidiFile (juce::File (l.trim()));
            outLines.add (juce::String ((int) m.ok) + "|" + juce::String (juce::roundToInt (m.seconds)) + "|" + juce::String (m.noteCount) + "|" + m.sourceFormat
                          + "|" + m.error + "|" + m.title + "|" + l.trim());
        }
        juce::File (args[2]).replaceWithText (outLines.joinIntoString ("\n"), false, false, "\n");
        return 0;
    }
    if (args.size() >= 2 && args[0] == "export")   // ArchivTest export <file> [more files]: MIDI / MusicXML / ABC round trips
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ma_export");
        dir.deleteRecursively();
        for (int i = 1; i < args.size(); ++i)
        {
            const juce::File src (args[i]);
            const auto orig = ma::analyseMidiFile (src);
            out ("== " + src.getFileName() + "  (" + juce::String (orig.noteCount) + " notes, " + orig.key + ")");
            for (auto f : { ma::ExportFormat::midi, ma::ExportFormat::musicXml, ma::ExportFormat::abc, ma::ExportFormat::dawProject, ma::ExportFormat::reaper })
            {
                juce::File written;
                juce::String err;
                const bool ok = ma::exportFile (src, f, dir, written, err);
                const auto back = ok ? ma::analyseMidiFile (written) : ma::MidiInfo {};
                const auto name = ma::exportFormatName (f);
                if (f == ma::ExportFormat::midi)
                    check (ok && back.ok && back.noteHash == orig.noteHash, name + ": identical notes " + err);
                else if (f == ma::ExportFormat::musicXml)
                    check (ok && back.ok && back.noteCount >= orig.noteCount * 9 / 10 && back.noteCount <= orig.noteCount + 2,
                           name + ": read back " + juce::String (back.noteCount) + " notes, key " + back.key + " " + err + back.error);
                else if (f == ma::ExportFormat::abc)
                    check (ok && back.ok && back.noteCount > 0, name + ": read back " + juce::String (back.noteCount) + " notes (melody per track) " + err + back.error);
                else
                {
                    // DAW projects: the same notes (pitch + start, 1/48 beat), same length, title kept
                    auto key = [] (const ma::MidiInfo& m) { std::multiset<juce::int64> k; for (auto& n : m.notes) k.insert ((juce::int64) n.pitch * 100000000 + juce::roundToInt (n.start * 48)); return k; };
                    const auto o2 = ma::analyseMidiFile (src, true);
                    const auto b2 = ok ? ma::analyseMidiFile (written, true) : ma::MidiInfo {};
                    check (ok && b2.ok && key (o2) == key (b2) && std::abs (b2.seconds - o2.seconds) < 0.6 && b2.title == o2.title && b2.drumNotes == o2.drumNotes,
                           name + ": " + juce::String (b2.noteCount) + "/" + juce::String (o2.noteCount) + " notes, " + juce::String (b2.seconds, 2) + "/" + juce::String (o2.seconds, 2)
                           + " s, drums " + juce::String (b2.drumNotes) + "/" + juce::String (o2.drumNotes) + ", title '" + b2.title + "' " + err + b2.error);
                }
                if (ok) out ("      " + written.getFileName() + "  " + juce::String (written.getSize()) + " bytes");
            }
        }
        out (failures == 0 ? "ALL OK" : juce::String (failures) + " FAILED");
        return failures;
    }
    if (args.size() >= 2 && args[0] == "folders")   // ArchivTest folders <db copy>: archive folders, protection, entries outside every folder
    {
        _putenv_s ("MIDIARCHIV_DB", juce::File (args[1]).getFullPathName().toRawUTF8());
        ma::Archive arch;
        arch.waitForIndex();
        out ("protected: " + arch.protectedFolders().joinIntoString (" | "));
        std::map<juce::String, int> orphans;
        std::vector<int> counts ((size_t) arch.folders.size(), 0);
        for (auto& e : arch.entries())
        {
            bool in = false;
            for (int i = 0; i < arch.folders.size(); ++i)
                if (e->path.startsWithIgnoreCase (arch.folders[i] + juce::File::getSeparatorString())) { ++counts[(size_t) i]; in = true; break; }
            if (! in)
            {
                juce::StringArray parts;
                parts.addTokens (e->path, "\\", {});
                ++orphans[parts.size() > 3 ? parts[0] + "\\" + parts[1] + "\\" + parts[2] : e->folder];
            }
        }
        for (int i = 0; i < arch.folders.size(); ++i) out ("folder: [" + arch.folders[i] + "]  " + juce::String (counts[(size_t) i]));
        for (auto& [k, n] : orphans) out ("outside: " + k + "  " + juce::String (n));
        if (args.contains ("clean"))
        {
            const int before = (int) arch.entries().size();
            const int removed = arch.removeOutsideFolders();
            const int left = (int) arch.entries().size(), outside = arch.countOutsideFolders();
            out ("before " + juce::String (before) + ", removed " + juce::String (removed) + ", left " + juce::String (left) + ", outside now " + juce::String (outside));
        }
        return 0;
    }
    if (args.size() >= 2 && args[0] == "remove")   // ArchivTest remove <some.mid>: removing folders (also during a scan) and entries without a folder (temp copies)
    {
        auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ma_remove");
        root.deleteRecursively();
        const juce::File src (args[1]);
        auto a = root.getChildFile ("Spiele"), b = root.getChildFile ("Musik");
        for (auto& d : { a, b }) { d.createDirectory(); for (int i = 0; i < 200; ++i) src.copyFileTo (d.getChildFile ("s" + juce::String (i) + ".mid")); }
        _putenv_s ("MIDIARCHIV_DB", root.getChildFile ("t.mdb").getFullPathName().toRawUTF8());
        {
            ma::Archive arch;
            arch.waitForIndex();
            const auto pa = arch.addFolder (a), pb = arch.addFolder (b);
            arch.setProtected (pa, true);
            arch.startScan();
            juce::Thread::sleep (30);
            const bool wasRunning = arch.isScanning();
            const int removed = arch.removeFolder (pa);   // while the scan runs
            arch.waitForScan();
            int inA = 0;
            for (auto& e : arch.entries()) inA += e->path.startsWithIgnoreCase (pa + "\\") ? 1 : 0;
            check (inA == 0 && ! arch.folders.contains (pa) && arch.countOutsideFolders() == 0,
                   "remove during scan (" + juce::String (wasRunning ? "running" : "already done") + "): " + juce::String (removed) + " removed, " + juce::String (inA) + " left");
            arch.startScan (false, { pb });
            arch.waitForScan();
            const int before = (int) arch.entries().size();
            arch.folders.removeString (pb, true);   // like the old "keep entries"
            check (arch.countOutsideFolders() == before && before == 200, "entries without folder counted: " + juce::String (arch.countOutsideFolders()));
            check (arch.removeOutsideFolders() == 200 && arch.entries().empty(), "entries without folder removed");
            check (a.getChildFile ("s0.mid").existsAsFile() && b.getChildFile ("s0.mid").existsAsFile(), "files themselves untouched");
        }
        root.deleteRecursively();
        out (failures == 0 ? "ALL OK" : juce::String (failures) + " FAILED");
        return failures;
    }
    if (args.size() >= 2 && args[0] == "protect")   // ArchivTest protect <some.mid>: write protection per folder (temp copies only)
    {
        auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ma_protect");
        root.deleteRecursively();
        const juce::File src (args[1]);
        auto a = root.getChildFile ("Spiele"), b = root.getChildFile ("Frei");
        for (auto& d : { a, b }) { d.createDirectory(); src.copyFileTo (d.getChildFile ("song.mid")); }
        juce::MemoryBlock before;
        a.getChildFile ("song.mid").loadFileAsData (before);
        const auto mtimeBefore = a.getChildFile ("song.mid").getLastModificationTime();
        _putenv_s ("MIDIARCHIV_DB", root.getChildFile ("t.mdb").getFullPathName().toRawUTF8());
        {
            ma::Archive arch;
            arch.waitForIndex();
            const auto pa = arch.addFolder (a), pb = arch.addFolder (b);
            arch.setProtected (pa, true);
            arch.startScan();
            arch.waitForScan();
            auto* ea = arch.byPath (a.getChildFile ("song.mid").getFullPathName());
            auto* eb = arch.byPath (b.getChildFile ("song.mid").getFullPathName());
            check (ea != nullptr && eb != nullptr && ea->has (ma::fProtected) && ! eb->has (ma::fProtected), "scan marks files in the protected folder");
            juce::String err;
            check (ea != nullptr && ! arch.setField (*ea, ma::Field::title, "Darf nicht", err) && err.isNotEmpty(), "writing into a protected folder is refused: " + err);
            juce::MemoryBlock after;
            a.getChildFile ("song.mid").loadFileAsData (after);
            check (after == before && a.getChildFile ("song.mid").getLastModificationTime() == mtimeBefore, "protected file byte-identical, date unchanged");
            check (eb != nullptr && arch.setField (*eb, ma::Field::title, "Darf", err) && eb->title == "Darf", "writing into a free folder works " + err);
            check (arch.isProtected (a.getChildFile ("x").getFullPathName()) && ! arch.isProtected (b.getChildFile ("x").getFullPathName())
                   && ! arch.isProtected (a.getFullPathName() + "2\\x.mid"), "isProtected: only below the folder (not a sibling with the same prefix)");
            arch.save();
        }
        {
            ma::Archive arch;   // protection survives a restart
            arch.waitForIndex();
            auto* ea = arch.byPath (a.getChildFile ("song.mid").getFullPathName());
            check (ea != nullptr && ea->has (ma::fProtected) && arch.protectedFolders().size() == 1, "protection kept after reload");
            arch.setProtected (arch.protectedFolders()[0], false);
            check (ea != nullptr && ! ea->has (ma::fProtected), "unprotect clears the mark");
        }
        root.deleteRecursively();
        out (failures == 0 ? "ALL OK" : juce::String (failures) + " FAILED");
        return failures;
    }
    if (args.size() >= 2 && args[0] == "partscan")   // ArchivTest partscan <some.mid>: scanning only chosen folders leaves the others untouched (temp copies only)
    {
        auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ma_partscan");
        root.deleteRecursively();
        const juce::File src (args[1]);
        auto a = root.getChildFile ("A"), b = root.getChildFile ("B"), c = root.getChildFile ("C");
        for (auto& d : { a, b, c }) { d.createDirectory(); src.copyFileTo (d.getChildFile ("one.mid")); src.copyFileTo (d.getChildFile ("two.mid")); }
        _putenv_s ("MIDIARCHIV_DB", root.getChildFile ("t.mdb").getFullPathName().toRawUTF8());
        {
            ma::Archive arch;
            arch.waitForIndex();
            arch.addFolder (a);
            const auto pb = arch.addFolder (b);
            check (pb == b.getFullPathName() && arch.addFolder (b.getChildFile ("sub")) == pb, "addFolder returns the covering folder");
            arch.startScan();
            arch.waitForScan();
            check (arch.entries().size() == 4, "first scan: A + B = 4 files (" + juce::String ((int) arch.entries().size()) + ")");
            b.getChildFile ("two.mid").deleteFile();
            const auto pc = arch.addFolder (c);
            arch.startScan (false, { pc });
            arch.waitForScan();
            auto* gone = arch.byPath (b.getChildFile ("two.mid").getFullPathName());
            check (arch.entries().size() == 6 && arch.byPath (c.getChildFile ("one.mid").getFullPathName()) != nullptr, "scan of C only adds C (" + juce::String ((int) arch.entries().size()) + ")");
            check (gone != nullptr && ! gone->has (ma::fMissing), "B was left out: its deleted file is not marked missing yet");
            arch.startScan (false, { pb });
            arch.waitForScan();
            gone = arch.byPath (b.getChildFile ("two.mid").getFullPathName());
            check (gone != nullptr && gone->has (ma::fMissing), "scan of B marks the deleted file missing");
            auto* keep = arch.byPath (a.getChildFile ("one.mid").getFullPathName());
            check (keep != nullptr && ! keep->has (ma::fMissing), "A untouched");
            arch.startScan (false, { "C:\\nicht\\im\\archiv" });
            check (! arch.isScanning(), "unknown folder alone starts nothing");
        }
        root.deleteRecursively();
        out (failures == 0 ? "ALL OK" : juce::String (failures) + " FAILED");
        return failures;
    }
    if (args.size() >= 1 && args[0] == "projects")   // hand-made DAWproject and REAPER files: clips, loops, references, scenes, takes, tempo
    {
        auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ma_projects");
        root.deleteRecursively();
        root.createDirectory();
        auto starts = [] (const ma::MidiInfo& m, int pitch) { juce::StringArray s; for (auto& n : m.notes) if (n.pitch == pitch) s.add (juce::String (n.start, 2)); return s.joinIntoString (" "); };
        auto zipOf = [] (const juce::File& f, const char* project, const char* meta) {
            juce::ZipFile::Builder z;
            z.addEntry (new juce::MemoryInputStream (project, strlen (project), true), 9, "project.xml", juce::Time::getCurrentTime());
            if (meta != nullptr) z.addEntry (new juce::MemoryInputStream (meta, strlen (meta), true), 9, "metadata.xml", juce::Time::getCurrentTime());
            f.deleteFile();
            juce::FileOutputStream os (f);
            z.writeToStream (os, nullptr);
        };
        // DAWproject: looped clip with playStart, clip by reference (drums), notes directly in a lane, tempo change, marker
        const char* p1 = R"~(<?xml version="1.0" encoding="UTF-8"?>
<Project version="1.0">
  <Application name="Bitwig Studio" version="5.2"/>
  <Transport><Tempo unit="bpm" value="100" id="tempo"/><TimeSignature numerator="4" denominator="4" id="sig"/></Transport>
  <Structure>
    <Track contentType="notes" loaded="true" id="t1" name="Lead"><Channel role="regular" id="c1"/></Track>
    <Track contentType="notes" loaded="true" id="t2" name="Beat"><Channel role="regular" id="c2"><Devices><BuiltinDevice deviceName="Drum Machine" deviceRole="instrument"/></Devices></Channel></Track>
  </Structure>
  <Arrangement id="a">
    <Lanes timeUnit="beats" id="l0">
      <Lanes track="t1" id="l1">
        <Clips id="cl1">
          <Clip time="4" duration="8" playStart="1" loopStart="0" loopEnd="4">
            <Notes id="n0">
              <Note time="0" duration="1" channel="0" key="60" vel="0.8" rel="0.8"/>
              <Note time="1" duration="1" channel="0" key="62" vel="0.8" rel="0.8"/>
              <Note time="3" duration="1" channel="0" key="64" vel="0.8" rel="0.8"/>
            </Notes>
          </Clip>
        </Clips>
        <Notes id="n2"><Note time="16" duration="2" channel="0" key="72" vel="1" rel="1"/></Notes>
      </Lanes>
      <Lanes track="t2" id="l2">
        <Clips id="cl2">
          <Clip time="0" duration="4" playStart="0" reference="n1"/>
          <Clip time="100" duration="4" enable="false">
            <Notes id="n1">
              <Note time="0" duration="0.5" channel="0" key="36" vel="0.9" rel="0.9"/>
              <Note time="1" duration="0.5" channel="0" key="38" vel="0.9" rel="0.9"/>
              <Note time="2" duration="0.5" channel="0" key="36" vel="0.9" rel="0.9"/>
              <Note time="3" duration="0.5" channel="0" key="38" vel="0.9" rel="0.9"/>
              <Note time="5" duration="0.5" channel="0" key="36" vel="0.9" rel="0.9"/>
            </Notes>
          </Clip>
        </Clips>
      </Lanes>
    </Lanes>
    <Markers id="m"><Marker time="8" name="Chorus"/></Markers>
    <TempoAutomation unit="bpm" id="ta"><Target parameter="tempo"/><RealPoint time="0" value="100" interpolation="hold"/><RealPoint time="8" value="50" interpolation="hold"/></TempoAutomation>
  </Arrangement>
  <Scenes/>
</Project>)~";
        const char* m1 = R"~(<?xml version="1.0" encoding="UTF-8"?><MetaData><Title>DP Test</Title><Artist>Tester</Artist><Year>2024</Year></MetaData>)~";
        auto f1 = root.getChildFile ("test.dawproject");
        zipOf (f1, p1, m1);
        auto d = ma::analyseMidiFile (f1, true);
        check (ma::isConvertibleFile (f1) && d.ok && d.sourceFormat == "DAWproject" && d.title == "DP Test" && d.artist == "Tester" && d.year == 2024, "DAWproject header " + d.error);
        check (d.noteCount == 11 && d.drumNotes == 4, "DAWproject notes " + juce::String (d.noteCount) + ", drums " + juce::String (d.drumNotes));
        check (starts (d, 60) == "7.00 11.00" && starts (d, 62) == "4.00 8.00" && starts (d, 64) == "6.00 10.00", "loop with playStart: 60@" + starts (d, 60) + " 62@" + starts (d, 62) + " 64@" + starts (d, 64));
        check (starts (d, 72) == "16.00" && std::abs (d.seconds - 16.8) < 0.05, "lane notes + tempo change: length " + juce::String (d.seconds, 3) + " s");
        // DAWproject: clip launcher only -> scenes one after another
        const char* p2 = R"~(<Project version="1.0"><Application name="Test" version="1"/>
  <Structure><Track contentType="notes" id="t1" name="Keys"/></Structure>
  <Arrangement id="a"><Lanes timeUnit="beats" id="l"/></Arrangement>
  <Scenes>
    <Scene id="s1" name="A"><Lanes id="x1"><ClipSlot track="t1" hasStop="true" id="cs1"><Clip time="0" duration="4" playStart="0"><Notes><Note time="0" duration="1" channel="0" key="60" vel="0.8" rel="0.8"/></Notes></Clip></ClipSlot></Lanes></Scene>
    <Scene id="s2" name="B"><Lanes id="x2"><ClipSlot track="t1" hasStop="true" id="cs2"><Clip time="0" duration="8" playStart="0"><Notes><Note time="2" duration="1" channel="0" key="67" vel="0.8" rel="0.8"/></Notes></Clip></ClipSlot></Lanes></Scene>
  </Scenes></Project>)~";
        auto f2 = root.getChildFile ("scenes.dawproject");
        zipOf (f2, p2, nullptr);
        auto s2 = ma::analyseMidiFile (f2, true);
        check (s2.ok && s2.noteCount == 2 && starts (s2, 60) == "0.00" && starts (s2, 67) == "6.00", "DAWproject scenes " + s2.error + " 67@" + starts (s2, 67));

        // REAPER: tempo envelope with metre, looped item with SOFFS, <X> event, muted item, selected take, metadata, notes
        auto f3 = root.getChildFile ("test.rpp");
        f3.replaceWithText (juce::String::fromUTF8 (R"~(<REAPER_PROJECT 0.1 "7.0/win64" 1700000000
  RIPPLE 0
  TEMPO 120 4 4
  <TEMPOENVEX
    ACT 1 -1
    PT 0 120 1
    PT 4 60 1 262147
  >
  MARKER 1 2 "Teil B" 0
  <RENDER_METADATA
    TAG ID3:TIT2 "RPP Test"
    TAG ID3:TPE1 'Art "X"'
  >
  <NOTES 0 2
    |Hallo Notiz
  >
  <TRACK {0A1B}
    NAME "Piano"
    <ITEM
      POSITION 1
      LENGTH 3
      LOOP 1
      SOFFS 0.5
      <SOURCE MIDI
        HASDATA 1 960 QN
        E 0 90 3c 64
        E 480 80 3c 00
        E 480 c0 05 00
        <X 0 0
          /wMEVGVzdA==
        >
        E 960 b0 7b 00
      >
    >
  >
  <TRACK
    NAME "Muted"
    <ITEM
      POSITION 0
      LENGTH 2
      MUTE 1 0
      <SOURCE MIDI
        HASDATA 1 960 QN
        E 0 90 40 64
        E 960 80 40 00
        E 0 b0 7b 00
      >
    >
  >
  <TRACK
    NAME "Takes"
    <ITEM
      POSITION 0
      LENGTH 2
      <SOURCE MIDI
        HASDATA 1 960 QN
        E 0 90 41 64
        E 960 80 41 00
        E 960 b0 7b 00
      >
      TAKE SEL
      NAME "take2"
      <SOURCE MIDI
        HASDATA 1 480 QN
        e 0 90 43 64
        e 480 80 43 00
        E 480 b0 7b 00
      >
    >
  >
>
)~"));
        auto r = ma::analyseMidiFile (f3, true);
        check (ma::isConvertibleFile (f3) && r.ok && r.sourceFormat == "REAPER" && r.title == "RPP Test" && r.artist == "Art \"X\"" && r.comment.contains ("Hallo Notiz"), "REAPER header " + r.error + " / " + r.artist + " / " + r.comment);
        check (r.noteCount == 4 && starts (r, 60) == "3.00 5.00 7.00" && starts (r, 67) == "0.00" && starts (r, 64).isEmpty() && starts (r, 65).isEmpty(),
               "REAPER loop/SOFFS/mute/take: 60@" + starts (r, 60) + " 67@" + starts (r, 67) + " n=" + juce::String (r.noteCount));
        check (r.seconds > 3.7 && r.seconds < 4.05 && r.timeSig == "4/4", "REAPER length " + juce::String (r.seconds, 3) + " sig " + r.timeSig);
        // tempo change at 4 s (beat 8) with 3/4: a note there must land at 4 s
        {
            auto f4 = root.getChildFile ("tempo.rpp");
            f4.replaceWithText ("<REAPER_PROJECT 0.1 \"7.0\" 0\n  TEMPO 120 4 4\n  <TEMPOENVEX\n    PT 0 120 1\n    PT 4 60 1 262147\n  >\n  <TRACK\n    <ITEM\n      POSITION 4\n      LENGTH 2\n"
                                "      <SOURCE MIDI\n        HASDATA 1 960 QN\n        E 0 90 3c 64\n        E 960 80 3c 00\n        E 0 b0 7b 00\n      >\n    >\n  >\n>\n");
            auto t = ma::analyseMidiFile (f4, true);
            check (t.ok && t.noteCount == 1 && starts (t, 60) == "8.00" && std::abs (t.seconds - 5.0) < 0.05 && t.tempoChanges, "REAPER tempo map: note@" + starts (t, 60) + " length " + juce::String (t.seconds, 3));
        }
        // writing DAW projects of the hand-made files and reading them back
        for (auto& src : { f1, f3 })
            for (auto fmt : { ma::ExportFormat::dawProject, ma::ExportFormat::reaper })
            {
                juce::File w;
                juce::String err;
                const bool ok = ma::exportFile (src, fmt, root.getChildFile ("out"), w, err);
                const auto o = ma::analyseMidiFile (src, true), b = ok ? ma::analyseMidiFile (w, true) : ma::MidiInfo {};
                check (ok && b.ok && b.noteCount == o.noteCount && b.title == o.title && std::abs (b.seconds - o.seconds) < 0.05 && starts (b, 60) == starts (o, 60),
                       src.getFileName() + " -> " + ma::exportFormatName (fmt) + ": " + juce::String (b.noteCount) + " notes, " + juce::String (b.seconds, 3) + " s " + err + b.error);
            }
        root.deleteRecursively();
        out (failures == 0 ? "ALL OK" : juce::String (failures) + " FAILED");
        return failures;
    }
    if (args.size() >= 1 && args[0] == "formats")   // hand-made ABC, kern, MUS, CMF files (no samples on disk)
    {
        auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ma_formats");
        root.deleteRecursively();
        root.createDirectory();
        auto notesOf = [] (const juce::File& f) { return ma::analyseMidiFile (f, true); };
        auto pitches = [] (const ma::MidiInfo& m) { juce::StringArray s; for (auto& n : m.notes) s.add (juce::String (n.pitch)); return s.joinIntoString (" "); };
        // ABC: simple tune
        auto abc1 = root.getChildFile ("entchen.abc");
        abc1.replaceWithText ("% test\nX:1\nT:Alle meine Entchen\nC:Volkslied\nM:2/4\nL:1/8\nQ:1/4=100\nK:C\nCDEF|G2G2|AAAA|G4|AAAA|G4|FFFF|E2E2|DDDD|C4|]\n\nX:2\nT:Second tune\nK:C\nCCCC|]\n");
        auto a = notesOf (abc1);
        check (a.ok && a.sourceFormat == "ABC" && a.title == "Alle meine Entchen" && a.composer == "Volkslied", "ABC header " + a.error);
        check (a.noteCount == 27 && ! a.notes.empty() && a.notes.front().pitch == 60, "ABC notes " + juce::String (a.noteCount));
        check (std::abs (a.seconds - 12.0) < 0.2 && std::abs (a.bpm - 100) < 0.5, "ABC tempo/length " + juce::String (a.seconds, 2));
        check (a.comment.contains ("2"), "ABC multi-tune note: " + a.comment);
        // ABC: key signature, broken rhythm, chord, triplet, tie
        auto abc2 = root.getChildFile ("details.abc");
        abc2.replaceWithText ("X:1\nT:Details\nL:1/4\nK:G\nF>G [CEG]2 (3ABc c-c|]\n");
        auto d = notesOf (abc2);
        check (d.ok && d.noteCount == 9, "ABC details count " + juce::String (d.noteCount) + " : " + pitches (d));
        if (d.notes.size() >= 2)
        {
            check (d.notes[0].pitch == 66 && std::abs (d.notes[0].len - 1.5f) < 0.01f && std::abs (d.notes[1].len - 0.5f) < 0.01f, "K:G gives F#, > dots the first note");
            const auto& last = d.notes.back();
            check (last.pitch == 72 && std::abs (last.len - 2.0f) < 0.01f, "tie c-c merged to a half note (len " + juce::String (last.len) + ")");
        }
        // Humdrum **kern
        auto krn = root.getChildFile ("choral.krn");
        krn.replaceWithText ("!!!OTL: Test Choral\n!!!COM: Anonymous\n**kern\t**kern\n*MM90\t*MM90\n*M3/4\t*M3/4\n4c\t2e\n4d\t.\n4e\t4g\n=1\t=1\n2.cc\t[2.g\n=2\t=2\n4B-\t4g]\n*-\t*-\n");
        auto k = notesOf (krn);
        check (k.ok && k.sourceFormat == "Humdrum" && k.title == "Test Choral" && k.composer == "Anonymous", "kern header " + k.error);
        check (k.noteCount == 8, "kern notes " + juce::String (k.noteCount) + " : " + pitches (k));
        check (pitches (k).contains ("58") && pitches (k).contains ("72") && std::abs (k.bpm - 90) < 0.5 && k.timeSig == "3/4", "kern pitches, tempo, metre");
        // DMX MUS: one note, 0.5 s
        {
            juce::MemoryOutputStream os;
            os.write ("MUS\x1a", 4);
            const juce::uint8 score[] = { 0x90, 0xBC, 100, 70, 0x80, 60, 0, 0x60 };
            os.writeShort ((short) sizeof (score)); os.writeShort (16); os.writeShort (1); os.writeShort (0); os.writeShort (0); os.writeShort (0);
            os.write (score, sizeof (score));
            root.getChildFile ("doom.mus").replaceWithData (os.getData(), os.getDataSize());
            auto m = notesOf (root.getChildFile ("doom.mus"));
            check (m.ok && m.sourceFormat == "MUS" && m.noteCount == 1 && m.notes.size() == 1 && m.notes[0].pitch == 60 && std::abs (m.seconds - 0.5) < 0.02, "MUS note " + m.error + " " + juce::String (m.seconds, 3));
        }
        // Creative CMF: header, title, one quarter note at 120 BPM
        {
            juce::MemoryOutputStream os;
            os.write ("CTMF", 4);
            os.writeShort (0x0101); os.writeShort (0); os.writeShort (64); os.writeShort (96); os.writeShort (192); os.writeShort (48); os.writeShort (0); os.writeShort (0);
            while (os.getDataSize() < 48) os.writeByte (0);
            os.write ("CMF Test", 8);
            while (os.getDataSize() < 64) os.writeByte (0);
            const juce::uint8 trk[] = { 0, 0x90, 60, 100, 0x60, 0x80, 60, 0, 0, 0xFF, 0x2F, 0 };
            os.write (trk, sizeof (trk));
            root.getChildFile ("song.cmf").replaceWithData (os.getData(), os.getDataSize());
            auto m = notesOf (root.getChildFile ("song.cmf"));
            check (m.ok && m.sourceFormat == "CMF" && m.title == "CMF Test" && m.noteCount == 1 && std::abs (m.seconds - 0.5) < 0.02, "CMF note/title " + m.error + " " + juce::String (m.seconds, 3));
        }
        // read-only: writing a field into a converted format is refused, the file stays untouched
        {
            juce::String err;
            const auto before = abc1.loadFileAsString();
            check (! ma::writeMidiField (abc1, ma::Field::title, "X", err) && abc1.loadFileAsString() == before, "ABC is read-only: " + err);
            check (ma::isConvertibleFile (abc1) && ! ma::isConvertibleFile (root.getChildFile ("none.txt")), "scanner accepts .abc");
        }
        root.deleteRecursively();
        out (failures == 0 ? "ALL OK" : juce::String (failures) + " FAILED");
        return failures;
    }
    if (args.size() >= 1 && args[0] == "ultrastar")   // UltraStar .txt: detection, encodings, relative mode, duet, writing, scan + duplicates
    {
        auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ma_ultrastar");
        root.deleteRecursively();
        root.createDirectory();
        // a made-up song, German words, 4 lines
        const char* syl[] = { "Ich ", "sing ", "dir ", "ein ", "Lied, ", "und ", "du ", "singst ", "mit ", "mir, ", "wir ", "sind ", "nicht ", "al", "lein, ", "das ", "ist ", "der ", "Tag, ", "die ", "Son", "ne ", "scheint ", "für ", "dich ", "und ", "mich ", "auf ", "ei", "nem ", "Weg, ", "Mäd", "chen ", "Bä", "ren ", "~" };
        const int pitches[] = { 0, 2, 4, 5, 7, 7, 9, 7, 5, 4, 2, 0, 4, 5, 7, 9, 11, 12, 11, 9, 7, 5, 4, 2, 0, 2, 4, 7, 5, 4, 2, 0, -1, 0, 2, 2 };
        const int n = (int) std::size (syl);
        auto song = [&] (const juce::String& title, int transpose, bool relative, bool duet, const juce::String& lang)
        {
            juce::String s;
            s << "#TITLE:" << title << "\r\n#ARTIST:Testband\r\n#LANGUAGE:" << lang << "\r\n#GENRE:Pop\r\n#YEAR:1999\r\n#MP3:x.mp3\r\n#BPM:300\r\n#GAP:2000\r\n";
            if (relative) s << "#RELATIVE:yes\r\n";
            int beat = 0, lineStart = 0;
            for (int i = 0; i < n; ++i)
            {
                if (duet && i == 0) s << "P1\r\n";
                if (duet && i == n / 2) s << "P2\r\n";
                const char type = i == 5 ? '*' : (i == 12 ? 'F' : ':');
                s << juce::String::charToString (type) << " " << (relative ? beat - lineStart : beat) << " 3 " << (pitches[i] + transpose) << " " << juce::String::fromUTF8 (syl[i]) << "\r\n";
                beat += 4;
                if (i % 9 == 8 && i + 1 < n)
                {
                    if (relative) { s << "- " << (beat - lineStart) << " " << (beat + 2 - lineStart) << "\r\n"; lineStart = beat + 2; }
                    else s << "- " << beat << "\r\n";
                    beat += 2;
                }
            }
            s << "E\r\n";
            return s;
        };
        auto writeUtf8Bom = [] (const juce::File& f, const juce::String& s) { juce::MemoryBlock mb; const juce::uint8 b[3] = { 0xEF, 0xBB, 0xBF }; mb.append (b, 3); mb.append (s.toRawUTF8(), strlen (s.toRawUTF8())); f.replaceWithData (mb.getData(), mb.getSize()); };
        auto writeLatin1 = [] (const juce::File& f, const juce::String& s) { juce::MemoryBlock mb; for (auto p = s.getCharPointer(); ! p.isEmpty();) { const auto c = (juce::uint8) p.getAndAdvance(); mb.append (&c, 1); } f.replaceWithData (mb.getData(), mb.getSize()); };
        auto dir = root.getChildFile ("Songs");
        dir.createDirectory();
        const auto fA = dir.getChildFile ("Testband - Sonnenlied.txt"), fB = dir.getChildFile ("Sonnenlied (Latin1).txt"),
                   fR = dir.getChildFile ("Sonnenlied relativ.txt"), fD = dir.getChildFile ("Sonnenlied Duett.txt"), fX = dir.getChildFile ("liesmich.txt");
        writeUtf8Bom (fA, song (juce::String::fromUTF8 ("Sonnenlied für Bären"), 0, false, false, "German"));
        writeLatin1 (fB, song (juce::String::fromUTF8 ("Sonnenlied für Bären"), 3, false, false, "Deutsch"));
        fR.replaceWithText (song ("Sonnenlied", 0, true, false, "German"));
        writeUtf8Bom (fD, song ("Sonnenlied Duett", 0, false, true, "German"));
        fX.replaceWithText ("# Notizen\nDas ist keine UltraStar-Datei.\n#TITLE fehlt mit Doppelpunkt\n");

        check (ma::isUltraStarFile (fA) && ma::isUltraStarFile (fB) && ma::isUltraStarFile (fR) && ma::isUltraStarFile (fD), "UltraStar files detected");
        check (! ma::isUltraStarFile (fX), "plain text file not detected");
        auto a = ma::analyseMidiFile (fA, true);
        printInfo (a);
        check (a.ok && a.ultrastar && a.karaoke, "UTF-8 file analysed as UltraStar");
        check (a.title == juce::String::fromUTF8 ("Sonnenlied für Bären") && a.artist == "Testband" && a.genre == "Pop" && a.year == 1999, "header fields");
        check (a.language == "Deutsch", "language German -> Deutsch");
        check (a.noteCount == n - 1, "pitched notes without freestyle: " + juce::String (a.noteCount));
        check (a.lyrics.contains (juce::String::fromUTF8 ("Ich sing dir ein Lied,")) && a.lyrics.contains ("allein,") && a.lyrics.contains ("\n") && ! a.lyrics.contains ("~"),
               "lyrics with line breaks: " + a.lyrics.replace ("\n", " / "));
        check (std::abs (a.bpm - 150.0) < 0.5, "BPM 300 shown as 150 (same timing): " + juce::String (a.bpm, 1));
        const double expect = 2.0 + (4.0 * (n - 1) + 2.0 * 3 + 3.0) * 60.0 / (300.0 * 4.0);   // GAP + last note end, 50 ms per beat
        check (std::abs (a.seconds - expect) < 0.1, "duration " + juce::String (a.seconds, 2) + " s ~ " + juce::String (expect, 2));
        check (! a.notes.empty() && a.notes.front().pitch == 60, "pitch 0 = middle C (60)");
        auto b = ma::analyseMidiFile (fB);
        check (b.ok && b.title == juce::String::fromUTF8 ("Sonnenlied für Bären") && b.lyrics.contains (juce::String::fromUTF8 ("Mädchen")), "Windows-1252 file decoded");
        auto r = ma::analyseMidiFile (fR);
        check (r.ok && r.noteHash == a.noteHash, "relative mode gives the same notes");
        auto d = ma::analyseMidiFile (fD);
        check (d.ok && d.noteCount == a.noteCount && d.numTracks == 3, "duet: two singer tracks (tracks=" + juce::String (d.numTracks) + ")");

        // writing keeps encoding, notes and line endings
        juce::String err;
        check (ma::writeMidiField (fB, ma::Field::comment, juce::String::fromUTF8 ("schön"), err), "write comment into Windows-1252 file " + err);
        juce::MemoryBlock raw;
        fB.loadFileAsData (raw);
        check (! juce::CharPointer_UTF8::isValidString ((const char*) raw.getData(), (int) raw.getSize()), "file stays Windows-1252");
        auto b2 = ma::analyseMidiFile (fB);
        check (b2.comment == juce::String::fromUTF8 ("schön") && b2.noteHash == b.noteHash, "comment read back, notes unchanged");
        check (ma::writeMidiField (fA, ma::Field::language, "Englisch", err) && ma::analyseMidiFile (fA).language == "Englisch", "language written as English");
        const auto txtA = fA.loadFileAsString();
        check (txtA.contains ("#LANGUAGE:English\r\n") && txtA.contains ("\r\n: 0 3 0 "), "header line replaced, CRLF kept");
        check (ma::writeMidiField (fA, ma::Field::genre, "", err) && ! fA.loadFileAsString().contains ("#GENRE"), "empty value removes the header line");
        check (ma::writeMidiField (fA, ma::Field::title, juce::String::fromUTF8 ("Neuer Titel ☀"), err) && ma::analyseMidiFile (fA).title == juce::String::fromUTF8 ("Neuer Titel ☀"), "UTF-8 title");
        check (ma::writeMidiField (fB, ma::Field::title, juce::String::fromUTF8 ("Sonne ☀"), err) && ma::analyseMidiFile (fB).title == juce::String::fromUTF8 ("Sonne ☀"), "symbol outside Windows-1252 switches to UTF-8");
        check (ma::analyseMidiFile (fB).comment == juce::String::fromUTF8 ("schön"), "umlaut survives the switch to UTF-8");
        check (! ma::writeMidiField (fA, ma::Field::composer, "X", err), "composer refused: " + err);
        check (! ma::writeMidiField (fA, ma::Field::title, "", err), "empty title refused");

        // MIDI copy for the DAW
        auto mid = ma::ultraStarMidiCopy (fD, root.getChildFile ("out"));
        juce::MidiFile mf;
        juce::FileInputStream in (mid);
        check (mid.existsAsFile() && mf.readFrom (in) && mf.getNumTracks() == 3, "MIDI copy readable by juce::MidiFile: " + mid.getFileName());
        int lyricEvents = 0;
        for (int t = 0; t < mf.getNumTracks(); ++t) for (auto* ev : *mf.getTrack (t)) if (ev->message.isMetaEvent() && ev->message.getMetaEventType() == 5) ++lyricEvents;
        check (lyricEvents >= n - 1, "lyric events in the MIDI copy: " + juce::String (lyricEvents));

        // scan: only UltraStar .txt, versions grouped as duplicates
        _putenv_s ("MIDIARCHIV_DB", root.getChildFile ("t.mdb").getFullPathName().toRawUTF8());
        {
            ma::Archive arch;
            arch.waitForIndex();
            arch.addFolder (dir);
            arch.startScan();
            arch.waitForScan();
            check (arch.entries().size() == 4, "scan: 4 UltraStar songs, plain .txt ignored (" + juce::String ((int) arch.entries().size()) + ")");
            auto* ea = arch.byPath (fA.getFullPathName());
            check (ea != nullptr && ea->has (ma::fUltraStar) && ma::cellText (*ea, ma::cFormat) == "UltraStar", "format column UltraStar");
            check (ea != nullptr && ea->groupSize >= 3, "relative / transposed versions grouped as duplicates (" + juce::String (ea != nullptr ? ea->groupSize : 0) + ")");
        }
        root.deleteRecursively();
        out (failures == 0 ? "ALL OK" : juce::String (failures) + " FAILED");
        return failures;
    }
    if (args.size() >= 1 && args[0] == "lang")
    {
        int failures = 0;
        auto check = [&] (ma::Lang l, const char* input, const char* expectGerman)
        {
            ma::setLanguage (l);
            const auto got = ma::keyInputToGerman (juce::String::fromUTF8 (input));
            const bool ok = got == juce::String::fromUTF8 (expectGerman);
            if (! ok) { out ("FEHLER " + ma::langCode (l) + ": " + juce::String::fromUTF8 (input) + " -> " + got + " (erwartet " + juce::String::fromUTF8 (expectGerman) + ")"); ++failures; }
        };
        using ma::Lang;
        check (Lang::en, "F# minor", "fis-Moll");   check (Lang::en, "Bb major", "B-Dur");     check (Lang::en, "B major", "H-Dur");
        check (Lang::en, "Am", "a-Moll");           check (Lang::en, "Eb", "Es-Dur");          check (Lang::en, "c sharp minor", "cis-Moll");
        check (Lang::en, "fis-Moll", "fis-Moll");   check (Lang::en, "Es-Dur", "Es-Dur");      check (Lang::en, "h-Moll", "h-Moll");
        check (Lang::fr, "fa# mineur", "fis-Moll"); check (Lang::fr, "si bémol majeur", "B-Dur"); check (Lang::fr, "ré mineur", "d-Moll");
        check (Lang::fr, "mib", "Es-Dur");          check (Lang::fr, "sol", "G-Dur");          check (Lang::fr, "la m", "a-Moll");
        check (Lang::fr, "B major", "H-Dur");       check (Lang::de, "B-Dur", "B-Dur");        check (Lang::de, "Am", "Am");
        // display round trip: every key name -> local -> back
        for (int l = 1; l <= 2; ++l)
            for (int t = 0; t < 12; ++t)
                for (bool m : { false, true })
                {
                    ma::setLanguage ((Lang) l);
                    const auto de = ma::keyNameOf (t, m);
                    const auto shown = ma::localKey (de);
                    const auto back = ma::keyInputToGerman (shown);
                    if (back != de) { out ("FEHLER Rundlauf " + ma::langCode ((Lang) l) + ": " + de + " -> " + shown + " -> " + back); ++failures; }
                }
        ma::setLanguage (Lang::en);
        out ("en: " + ma::localKey (juce::String::fromUTF8 ("fis-Moll?")) + " | " + ma::localMode ("fis-Moll?") + " | " + ma::localScale (juce::String::fromUTF8 ("Harmonisch Moll · 7"))
             + " | " + ma::localLanguageName (juce::String::fromUTF8 ("Französisch")) + " | " + ma::T ("%1 Dateien  ·  %2 angezeigt  ·  %3 Dubletten-Gruppen", 3, 2, 1));
        ma::setLanguage (Lang::fr);
        out (juce::String::fromUTF8 ("fr: ") + ma::localKey ("B-Dur") + " | " + ma::localMode ("a-Moll") + " | " + ma::localScale (juce::String::fromUTF8 ("Erweitert · 8"))
             + " | " + ma::localLanguageName ("Englisch") + " | " + ma::TS ("Datei nicht lesbar"));
        out (failures == 0 ? "lang ALL OK" : "lang FAILURES " + juce::String (failures));
        return failures;
    }
    if (args.size() >= 2 && args[0] == "notdup")
    {
        juce::File db (args[1]);
        _putenv_s ("MIDIARCHIV_DB", db.getFullPathName().toRawUTF8());
        int failures = 0;
        std::vector<std::pair<int, int>> done;
        {
            ma::Archive arch;
            arch.waitForIndex();
            // pick pairs: indirect (group only) and direct links, in groups of size >= 4
            std::map<int, std::vector<int>> groups;
            for (auto& e : arch.entries()) if (e->group) groups[e->group].push_back (e->id);
            int indirect = 0, direct = 0;
            for (auto& [g, v] : groups)
            {
                if (v.size() < 4 || (indirect >= 3 && direct >= 3)) continue;
                const int a = v[0];
                auto la = arch.linksOf (a);
                for (int b : v)
                {
                    if (b == a) continue;
                    const bool isDirect = std::any_of (la.begin(), la.end(), [b] (auto& l) { return l.other == b; });
                    if (isDirect ? direct >= 3 : indirect >= 3) continue;
                    const int before = arch.byId (a)->groupSize;
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    arch.markNotDuplicate (a, b);
                    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                    auto* ea = arch.byId (a); auto* eb = arch.byId (b);
                    const bool ok = ea->group == 0 || ea->group != eb->group;
                    out (juce::String (isDirect ? "direct  " : "indirect") + " size " + juce::String (before) + " -> A " + juce::String (ea->groupSize)
                         + " / B " + juce::String (eb->groupSize) + "  " + juce::String (ms, 1) + " ms  " + (ok ? "ok" : "FEHLER"));
                    failures += ok ? 0 : 1;
                    done.push_back ({ a, b });
                    (isDirect ? direct : indirect)++;
                    break;
                }
            }
            arch.save();
        }
        {
            ma::Archive arch;       // reload + full rebuild must keep the separation
            arch.waitForIndex();
            arch.rebuildDuplicates();
            for (auto [a, b] : done)
            {
                auto* ea = arch.byId (a); auto* eb = arch.byId (b);
                const bool ok = ea->group == 0 || ea->group != eb->group;
                if (! ok) { out ("nach Neuaufbau wieder verbunden: " + ea->name + " / " + eb->name); ++failures; }
            }
        }
        out (failures == 0 ? "notdup ALL OK" : "notdup FAILURES " + juce::String (failures));
        return failures;
    }
    if (args.size() >= 2 && (args[0] == "scan" || args[0] == "groups"))
    {
        juce::File db (args[1]);
        if (args[0] == "scan") db.deleteFile();
        _putenv_s ("MIDIARCHIV_DB", db.getFullPathName().toRawUTF8());
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        ma::Archive arch;
        out ("loaded " + juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 2) + "s, groups from db=" + juce::String (arch.duplicateGroups()));
        arch.waitForIndex();
        out ("index ready " + juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 2) + "s");
        if (args[0] == "scan")
        {
            for (int i = 2; i < args.size(); ++i) arch.addFolder (juce::File (args[i]));
            arch.startScan();
            arch.waitForScan();
        }
        arch.waitForIndex();
        arch.save();
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        int errors = 0, kar = 0, fromName = 0;
        for (auto& e : arch.entries()) { errors += e->has (ma::fError); kar += e->lyrics.isNotEmpty(); fromName += e->has (ma::fTitleFromName); }
        out ("entries=" + juce::String ((int) arch.entries().size()) + " errors=" + juce::String (errors) + " lyrics=" + juce::String (kar)
             + " titleFromName=" + juce::String (fromName) + " groups=" + juce::String (arch.duplicateGroups()) + " time=" + juce::String ((t1 - t0) / 1000.0, 1) + "s");
        const auto t2 = juce::Time::getMillisecondCounterHiRes();
        arch.rebuildDuplicates();
        out ("rebuildDuplicates " + juce::String ((juce::Time::getMillisecondCounterHiRes() - t2) / 1000.0, 2) + "s");
        std::map<int, std::vector<ma::Entry*>> groups;
        for (auto& e : arch.entries()) if (e->group) groups[e->group].push_back (e.get());
        int shown = 0;
        const int limit = args.size() > 2 && args[0] == "groups" ? args[2].getIntValue() : 25;
        int kinds[4] {};
        for (auto& e : arch.entries()) ++kinds[e->dupKind];
        out ("kinds: identical=" + juce::String (kinds[3]) + " sameNotes=" + juce::String (kinds[2]) + " similar=" + juce::String (kinds[1]));
        for (auto& [g, v] : groups)
        {
            if (shown++ >= limit) break;
            out ("--- group " + juce::String (g) + " (" + juce::String ((int) v.size()) + ")");
            for (auto* e : v)
                out ("   " + ma::dupText (*e) + "  " + e->folder.fromLastOccurrenceOf ("\\", false, false) + "\\" + e->name + "  [" + e->title + " / " + e->artist + "]");
        }
        return 0;
    }
    out ("usage: ArchivTest info|write <file> | scan <db> <folder...> | groups <db> [n]");
    return 1;
}
