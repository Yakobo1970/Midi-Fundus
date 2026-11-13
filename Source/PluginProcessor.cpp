#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MidiData.h"
#include "Lang.h"
#include "Lang.h"

MidiArchivProcessor::MidiArchivProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    loadSoundFontAsync ({});
    if (auto x = juce::XmlDocument::parse (globalSettingsFile()))
    {
        sync = x->getBoolAttribute ("hostSync", false);
        tempoSync = x->getBoolAttribute ("hostTempo", true);
    }
    startTimerHz (25);
}

// The program was called MIDIARCHIV before: the standalone's settings (window size, audio device, view) move over once.
// Runs at load time because the standalone opens its settings file before the processor exists.
static const bool settingsMigrated = []
{
    const auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    const auto oldF = dir.getChildFile ("MIDIARCHIV").getChildFile ("MIDIARCHIV.settings");
    const auto newF = dir.getChildFile ("Midi-Fundus").getChildFile ("Midi-Fundus.settings");   // JUCE: %APPDATA%\<name>\<name>.settings
    return oldF.existsAsFile() && ! newF.exists() && newF.getParentDirectory().createDirectory() && oldF.copyFileTo (newF);
}();

juce::File MidiArchivProcessor::globalSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MIDIARCHIV").getChildFile ("plugin.xml");
}

void MidiArchivProcessor::saveGlobalSettings()
{
    auto f = globalSettingsFile();
    std::unique_ptr<juce::XmlElement> x = juce::XmlDocument::parse (f);
    if (x == nullptr) x = std::make_unique<juce::XmlElement> ("MIDIARCHIV");
    x->setAttribute ("hostSync", sync.load());
    x->setAttribute ("hostTempo", tempoSync.load());
    f.getParentDirectory().createDirectory();
    x->writeTo (f);
}

void MidiArchivProcessor::setHostSync (bool on)
{
    sync = on;
    saveGlobalSettings();
    juce::String p;
    { const juce::ScopedLock sl (lock); p = cuePath; }
    if (on && p.isNotEmpty() && ! playing.load()) load (juce::File (p), false, 0.0);   // arm the cued file
}

void MidiArchivProcessor::setHostTempo (bool on)
{
    tempoSync = on;
    saveGlobalSettings();
    const juce::ScopedLock sl (lock);
    resync = true;
}

void MidiArchivProcessor::setCue (const juce::String& path)
{
    { const juce::ScopedLock sl (lock); cuePath = path; }
    uiState.setProperty ("cue", path, nullptr);
    if (! sync.load() || path.isEmpty()) return;
    // a preview started by hand (DAW stopped) is not interrupted by selecting another file
    if (playing.load() && ! hostWasPlaying.load()) { armed = false; return; }
    // DAW running: the newly selected file takes over at the current DAW position; DAW stopped: armed for the next Play
    load (juce::File (path), hostWasPlaying.load(), 0.0);
}

void MidiArchivProcessor::timerCallback()
{
    const int edge = hostEdge.exchange (0);
    if (edge == 0 || ! sync.load()) return;
    if (edge > 0 && ! playing.load())   // cue was not armed in time: load now, it joins at the DAW position
    {
        juce::String p;
        { const juce::ScopedLock sl (lock); p = cuePath; }
        if (p.isNotEmpty()) load (juce::File (p), true, 0.0);
    }
    if (onHostTransport) onHostTransport();
}

MidiArchivProcessor::~MidiArchivProcessor()
{
    if (sfThread.joinable()) sfThread.join();
}

void MidiArchivProcessor::loadSoundFontAsync (const juce::File& sf2)
{
    if (sfThread.joinable()) sfThread.join();
    sfPath = sf2.getFullPathName();
    sfThread = std::thread ([this, sf2] {
        const auto file = sf2 == juce::File() ? SoundFontPlayer::defaultSoundFont() : sf2;
        juce::String err;
        if (file == juce::File()) err = ma::T ("GeneralUser-GS.sf2 nicht gefunden");
        else sfPlayer.load (file, err);
        const juce::ScopedLock sl (lock);
        sfError = err;
    });
}

void MidiArchivProcessor::setSoundMode (int m)
{
    m = juce::jlimit ((int) sndSoundFont, (int) sndNone, m);
    if (m == sndWindowsGm && ! setWindowsGm (true)) m = sndSoundFont;
    if (m != sndWindowsGm) setWindowsGm (false);
    mode = m;
    flush = true;
}

static juce::String findGmDevice()
{
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.name.containsIgnoreCase ("GS Wavetable")) return d.identifier;
    return {};
}

bool MidiArchivProcessor::windowsGmAvailable() { return findGmDevice().isNotEmpty(); }

bool MidiArchivProcessor::setWindowsGm (bool on)
{
    std::unique_ptr<juce::MidiOutput> dev;
    if (on)
    {
        const auto id = findGmDevice();
        if (id.isNotEmpty()) dev = juce::MidiOutput::openDevice (id);
        if (dev != nullptr) dev->startBackgroundThread();
    }
    {
        const juce::SpinLock::ScopedLockType sl (gmLock);
        std::swap (gmOut, dev);
        gmOn = gmOut != nullptr;
    }
    if (dev != nullptr)
    {
        for (int ch = 1; ch <= 16; ++ch) { dev->sendMessageNow (juce::MidiMessage::allNotesOff (ch)); dev->sendMessageNow (juce::MidiMessage::allSoundOff (ch)); }
        dev->stopBackgroundThread();
    }
    return gmOn.load() == on;
}

bool MidiArchivProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() || l.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void MidiArchivProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    synth.prepare (sampleRate);
    sfPlayer.prepare (sampleRate, samplesPerBlock);
    sfBuf.ensureSize (4096);
    scratch.setSize (2, juce::jmax (512, samplesPerBlock));
}

bool MidiArchivProcessor::play (const juce::File& f, double from) { return load (f, true, from); }

double MidiArchivProcessor::beatToSec (double b) const
{
    if (tempoMap.empty()) return b * 0.5;
    size_t i = 0;
    while (i + 1 < tempoMap.size() && tempoMap[i + 1].beat <= b) ++i;
    const auto& t = tempoMap[i];
    return t.sec + (b - t.beat) * t.spb;
}

// file timeline -> DAW position (beat 0 of the file = start of the DAW project)
void MidiArchivProcessor::placeAt (double beat, juce::MidiBuffer& out)
{
    nextEvent = (int) (std::lower_bound (beats.begin(), beats.end(), beat) - beats.begin());
    pos = juce::jlimit (0.0, len.load(), beatToSec (beat));
    std::array<int, 16> prog;
    prog.fill (-1);
    for (int i = 0; i < nextEvent; ++i)
        if (auto& m = seq.getEventPointer (i)->message; m.isProgramChange()) prog[(size_t) m.getChannel() - 1] = m.getProgramChangeNumber();
    allOff (out);
    for (int ch = 0; ch < 16; ++ch)
    {
        program[(size_t) ch] = juce::jmax (0, prog[(size_t) ch]);
        if (prog[(size_t) ch] >= 0 && midiOut) out.addEvent (juce::MidiMessage::programChange (ch + 1, prog[(size_t) ch]), 0);
    }
}

bool MidiArchivProcessor::load (const juce::File& f, bool start, double from)
{
    juce::MemoryBlock data, smf;
    size_t off = 0;
    if (! f.loadFileAsData (data) || ! ma::extractSmf (data, smf, off)) return false;
    juce::MemoryInputStream is (smf, false);
    juce::MidiFile mf;
    if (! mf.readFrom (is, true)) return false;
    const int ppqn = mf.getTimeFormat();
    if (ppqn <= 0) mf.convertTimestampTicksToSeconds();   // SMPTE timing: no beats
    juce::MidiMessageSequence merged;
    for (int t = 0; t < mf.getNumTracks(); ++t) merged.addSequence (*mf.getTrack (t), 0.0);
    merged.updateMatchedPairs();
    std::vector<double> bt;
    std::vector<TempoPoint> tmap;
    if (ppqn > 0)   // ticks -> beats + seconds (own tempo map)
    {
        tmap.push_back ({ 0.0, 0.0, 0.5 });
        bt.resize ((size_t) merged.getNumEvents());
        for (int i = 0; i < merged.getNumEvents(); ++i)
        {
            auto& m = merged.getEventPointer (i)->message;
            const double b = m.getTimeStamp() / ppqn;
            const auto& t = tmap.back();
            const double sec = t.sec + (b - t.beat) * t.spb;
            bt[(size_t) i] = b;
            m.setTimeStamp (sec);
            if (m.isTempoMetaEvent() && m.getTempoSecondsPerQuarterNote() > 0.0)
            {
                if (b <= t.beat) tmap.back() = { t.beat, t.sec, m.getTempoSecondsPerQuarterNote() };
                else tmap.push_back ({ b, sec, m.getTempoSecondsPerQuarterNote() });
            }
        }
    }
    {
        const juce::ScopedLock sl (lock);
        seq.swapWith (merged);
        beats.swap (bt);
        tempoMap.swap (tmap);
        resync = true;
        armed = true;
        currentPath = f.getFullPathName();
        len = seq.getEndTime();
        pos = juce::jlimit (0.0, len.load(), from);
        nextEvent = seq.getNextIndexAtTime (pos.load());
        program.fill (0);
        // programs set before the start point
        for (int i = 0; i < nextEvent; ++i)
            if (auto& m = seq.getEventPointer (i)->message; m.isProgramChange()) program[(size_t) m.getChannel() - 1] = m.getProgramChangeNumber();
        flush = true;
        if (start) playing = true;
    }
    return true;
}

void MidiArchivProcessor::stop()
{
    playing = false;
    flush = true;
}

void MidiArchivProcessor::seek (double seconds)
{
    const juce::ScopedLock sl (lock);
    pos = juce::jlimit (0.0, len.load(), seconds);
    nextEvent = seq.getNextIndexAtTime (pos.load());
    for (int i = 0; i < nextEvent; ++i)
        if (auto& m = seq.getEventPointer (i)->message; m.isProgramChange()) program[(size_t) m.getChannel() - 1] = m.getProgramChangeNumber();
    flush = true;
}

static int soundFor (int channel, int prog)
{
    using namespace kr;
    if (channel == 9) return sKit;
    if (prog < 8) return sKeys;
    if (prog < 16) return sBell;
    if (prog < 24) return sOrgan;
    if (prog < 32) return sPluck;
    if (prog < 38) return sSub;
    if (prog < 40) return sAcid;
    if (prog < 56) return sPad;
    if (prog < 80) return sSawLead;
    if (prog < 88) return sSquareLead;
    if (prog < 104) return sPad;
    if (prog < 112) return sPluck;
    if (prog < 120) return sBell;
    return sPad;
}

void MidiArchivProcessor::allOff (juce::MidiBuffer& out)
{
    for (int ch = 1; ch <= 16; ++ch)
    {
        out.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
        out.addEvent (juce::MidiMessage::controllerEvent (ch, 64, 0), 0);
        out.addEvent (juce::MidiMessage::pitchWheel (ch, 8192), 0);
    }
    for (auto& h : held) h.fill (false);
    synth.releaseAll();
    sfReset = true;
    if (gmOn)
        for (int ch = 1; ch <= 16; ++ch)
        {
            gmBuf.addEvent (juce::MidiMessage::allSoundOff (ch), 0);
            gmBuf.addEvent (juce::MidiMessage::controllerEvent (ch, 121, 0), 0);   // reset controllers
            gmBuf.addEvent (juce::MidiMessage::controllerEvent (ch, 7, 100), 0);
            gmBuf.addEvent (juce::MidiMessage::programChange (ch, program[(size_t) ch - 1]), 0);
        }
}

void MidiArchivProcessor::handle (const juce::MidiMessage& m, juce::MidiBuffer& out, int sample)
{
    if (m.isMetaEvent() || m.isSysEx()) return;
    const int ch = m.getChannel() - 1;
    if (midiOut) out.addEvent (m, sample);
    if (m.isProgramChange()) program[(size_t) ch] = m.getProgramChangeNumber();
    const int md = mode.load();
    if (md == sndWindowsGm && gmOn) gmBuf.addEvent (m, sample);
    if (md == sndSoundFont) sfBuf.addEvent (m, sample);
    if (md != sndKreisel) return;
    if (m.isNoteOn())
    {
        synth.noteOn (ch, soundFor (ch, program[(size_t) ch]), m.getNoteNumber(), m.getVelocity(), 0.6f);
        held[(size_t) ch][(size_t) m.getNoteNumber()] = true;
    }
    else if (m.isNoteOff())
    {
        synth.noteOff (ch, m.getNoteNumber());
        held[(size_t) ch][(size_t) m.getNoteNumber()] = false;
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff()) synth.releaseAll();
}

void MidiArchivProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    buffer.clear();
    midi.clear();   // incoming MIDI is not passed through; the output carries the preview
    juce::MidiBuffer out;
    if (flush.exchange (false)) allOff (out);

    bool hostPlaying = false, havePpq = false;
    double ppq = 0.0, bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (const auto info = ph->getPosition())
        {
            hostPlaying = info->getIsPlaying();
            if (const auto p = info->getPpqPosition()) { ppq = *p; havePpq = true; }
            if (const auto b = info->getBpm(); b && *b > 1.0) bpm = *b;
        }
    if (hostPlaying != hostWasPlaying.load())
    {
        hostWasPlaying = hostPlaying;
        hostEdge = hostPlaying ? 1 : -1;
        if (sync.load())
        {
            if (hostPlaying) { if (armed.load()) { playing = true; expectPpq = -1.0; } }
            else if (playing.load()) { playing = false; allOff (out); }
        }
    }
    const bool follow = sync.load() && hostPlaying && havePpq;

    if (playing.load())
    {
        const juce::ScopedTryLock sl (lock);
        bool freeRun = true;
        if (sl.isLocked())
        {
            const double hostSpb = 60.0 / bpm, blockBeats = n / sr / hostSpb;
            if (follow)
            {
                // DAW start, loop or jump: place the file at the DAW position (file beat 0 = project start)
                if (resync || std::abs (ppq - expectPpq) > 0.01)
                {
                    resync = false;
                    if (! beats.empty()) placeAt (juce::jmax (0.0, ppq), out);
                    else { pos = juce::jlimit (0.0, len.load(), juce::jmax (0.0, ppq) * hostSpb); nextEvent = seq.getNextIndexAtTime (pos.load()); allOff (out); }
                }
                expectPpq = ppq + blockBeats;
                if (tempoSync.load() && ! beats.empty())   // DAW tempo drives the file
                {
                    freeRun = false;
                    const double b1 = ppq + blockBeats;
                    while (nextEvent < (int) beats.size() && beats[(size_t) nextEvent] < b1)
                    {
                        const int s = juce::jlimit (0, n - 1, (int) ((beats[(size_t) nextEvent] - ppq) * hostSpb * sr));
                        handle (seq.getEventPointer (nextEvent)->message, out, s);
                        ++nextEvent;
                    }
                    pos = juce::jlimit (0.0, len.load(), beatToSec (b1));
                }
            }
        }
        if (sl.isLocked() && freeRun)
        {
            const double start = pos.load(), end = start + n / sr;
            while (nextEvent < seq.getNumEvents())
            {
                const auto& m = seq.getEventPointer (nextEvent)->message;
                if (m.getTimeStamp() >= end) break;
                const int s = juce::jlimit (0, n - 1, (int) ((m.getTimeStamp() - start) * sr));
                handle (m, out, s);
                ++nextEvent;
            }
            pos = juce::jmin (end, len.load() + 0.5);
            if (end >= len.load() + 0.5 && ! follow) { playing = false; allOff (out); }   // following the DAW: wait silently (loop)
        }
    }

    if (scratch.getNumSamples() < n) scratch.setSize (2, n, false, false, true);
    scratch.clear();
    synth.render (scratch.getWritePointer (0), scratch.getWritePointer (1), n);
    if (sfReset.exchange (false)) sfPlayer.reset (program);
    sfPlayer.process (sfBuf, scratch.getWritePointer (0), scratch.getWritePointer (1), n, 1.0f);
    sfBuf.clear();
    const float g = volume.load() * 1.1f;
    for (int c = 0; c < buffer.getNumChannels(); ++c)
    {
        auto* d = buffer.getWritePointer (c);
        const auto* s = scratch.getReadPointer (juce::jmin (c, 1));
        for (int i = 0; i < n; ++i) d[i] = std::tanh (s[i] * g);
    }
    midi.swapWith (out);
    if (! gmBuf.isEmpty())
    {
        const juce::SpinLock::ScopedTryLockType sl (gmLock);
        if (sl.isLocked() && gmOut != nullptr) gmOut->sendBlockOfMessages (gmBuf, juce::Time::getMillisecondCounterHiRes(), sr);
        gmBuf.clear();
    }
}

void MidiArchivProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto t = uiState.createCopy();
    t.setProperty ("soundMode", mode.load(), nullptr);
    t.setProperty ("soundFont", sfPath, nullptr);
    t.setProperty ("midiOut", midiOut.load(), nullptr);
    t.setProperty ("volume", volume.load(), nullptr);
    juce::MemoryOutputStream os (destData, false);
    t.writeToStream (os);
}

void MidiArchivProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto t = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (! t.isValid()) return;
    uiState = t;
    setSoundMode ((int) t.getProperty ("soundMode", (int) sndSoundFont));
    const juce::String path = t.getProperty ("soundFont").toString();
    if (path != sfPath) loadSoundFontAsync (juce::File (path));
    midiOut = (bool) t.getProperty ("midiOut", true);
    const juce::String cue = t.getProperty ("cue").toString();
    { const juce::ScopedLock sl (lock); cuePath = cue; }
    if (sync.load() && cue.isNotEmpty() && ! playing.load()) load (juce::File (cue), false, 0.0);   // armed for the DAW's Play
    volume = (float) t.getProperty ("volume", 0.7f);
}

juce::AudioProcessorEditor* MidiArchivProcessor::createEditor() { return new MidiArchivEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MidiArchivProcessor(); }
