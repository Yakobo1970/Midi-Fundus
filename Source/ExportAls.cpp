// MIDIARCHIV – "Convert to" Ableton Live Set (.als = gzip'ed XML).
// Ableton's format is not documented, so the set is built from the templates of the installed Live
// (Resources/Builtin/Templates/DefaultLiveSet.als and the MIDI clip structure of a Core Library demo set) – the same
// approach as in Spurwerk. Nothing of Ableton is shipped with Midi-Fundus; without an installed Live there is no export.
// One MIDI track per MIDI track and channel (no instrument – the track name carries the GM program, the clip sends it),
// one arrangement clip over the whole song with notes and pitch bend, tempo and metre changes as automation, markers as locators.
#include "Export.h"
#include "MidiData.h"
#include "Lang.h"
#include "GmSamples.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <map>
#include <set>
#include <mutex>

namespace ma::daw
{
namespace
{
using Xml = juce::XmlElement;

std::unique_ptr<Xml> loadGzXml (const juce::File& f)
{
    juce::FileInputStream in (f);
    if (! in.openedOk()) return nullptr;
    juce::GZIPDecompressorInputStream gz (&in, false, juce::GZIPDecompressorInputStream::gzipFormat);
    return juce::parseXML (gz.readEntireStreamAsString());
}

Xml* child (Xml* e, std::initializer_list<const char*> path)
{
    for (auto* p : path)
    {
        if (e == nullptr) return nullptr;
        e = e->getChildByName (p);
    }
    return e;
}

void setValue (Xml* e, const juce::String& v) { if (e != nullptr) e->setAttribute ("Value", v); }
juce::String num (double v) { return juce::String (v, 6).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."); }

Xml* findFirst (Xml* e, const juce::String& tag)
{
    if (e == nullptr) return nullptr;
    if (e->hasTagName (tag)) return e;
    for (auto* c : e->getChildIterator())
        if (auto* r = findFirst (c, tag)) return r;
    return nullptr;
}

Xml* findFirstWithin (Xml* e, const juce::String& outer, const juce::String& inner)
{
    if (e == nullptr) return nullptr;
    if (e->hasTagName (outer)) if (auto* r = findFirst (e, inner)) return r;
    for (auto* c : e->getChildIterator())
        if (auto* r = findFirstWithin (c, outer, inner)) return r;
    return nullptr;
}

// automation / modulation targets and pointees need set-wide unique ids
void renumber (Xml& e, int& next)
{
    const auto tag = e.getTagName();
    if (e.hasAttribute ("Id") && (tag.endsWith ("Target") || tag.startsWith ("ControllerTargets.") || tag == "Pointee"))
        e.setAttribute ("Id", next++);
    for (auto* c : e.getChildIterator()) renumber (*c, next);
}

// Live stores a metre as (numerator - 1) + 99 * log2 (denominator): 4/4 = 201
int liveMetre (int numerator, int denominator)
{
    int l = 0;
    while ((1 << (l + 1)) <= juce::jmax (1, denominator)) ++l;
    return juce::jlimit (1, 99, numerator) - 1 + 99 * l;
}

const int kLiveColours[8] = { 14, 1, 4, 6, 9, 12, 16, 19 };

struct Templates
{
    juce::File resources;
    std::unique_ptr<Xml> set, midiClip;
    std::unique_ptr<Xml> sampler, part;   // Sampler device + one of its sample zones (for the GM placeholder instruments)
};

juce::File findAbletonResources()
{
    juce::Array<juce::File> dirs;
    for (const auto& base : { juce::File ("C:\\ProgramData\\Ableton"), juce::File ("/Applications") })
        for (const auto& d : base.findChildFiles (juce::File::findDirectories, false, "Live *"))
            for (auto res : { d.getChildFile ("Resources"), d.getChildFile ("Contents/App-Resources") })
                if (res.getChildFile ("Builtin/Templates/DefaultLiveSet.als").existsAsFile())
                    dirs.add (res);
    if (dirs.isEmpty()) return {};
    std::sort (dirs.begin(), dirs.end(), [] (const juce::File& a, const juce::File& b) { return a.getFullPathName().compareNatural (b.getFullPathName()) > 0; });
    return dirs.getFirst();
}

// loaded once per process (the demo-set search reads a few MB)
const Templates& templates()
{
    static std::mutex m;
    static Templates t;
    static bool loaded = false;
    const std::lock_guard<std::mutex> lock (m);
    if (loaded) return t;
    loaded = true;
    t.resources = findAbletonResources();
    if (t.resources == juce::File()) return t;
    t.set = loadGzXml (t.resources.getChildFile ("Builtin/Templates/DefaultLiveSet.als"));
    auto files = t.resources.getChildFile ("Core Library").findChildFiles (juce::File::findFiles, true, "*.als");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getSize() < b.getSize(); });
    for (const auto& f : files)
    {
        if (f.getSize() > 8 * 1024 * 1024) break;
        auto demo = loadGzXml (f);
        if (auto* c = findFirstWithin (demo.get(), "ArrangerAutomation", "MidiClip"))
        {
            t.midiClip = std::make_unique<Xml> (*c);
            break;
        }
    }
    if (auto doc = loadGzXml (t.resources.getChildFile ("Core Library/Defaults/Instruments/Sampler.adv")))
        if (auto* s = doc->getChildByName ("MultiSampler")) t.sampler = std::make_unique<Xml> (*s);
    if (t.sampler != nullptr)
    {
        auto presets = t.resources.getChildFile ("Core Library/Devices/Instruments/Sampler").findChildFiles (juce::File::findFiles, true, "*.adv");
        presets.addArray (t.resources.getChildFile ("Core Library").findChildFiles (juce::File::findFiles, true, "*.adv"));
        for (const auto& f : presets)
        {
            if (f.getSize() > 2 * 1024 * 1024) continue;
            auto doc = loadGzXml (f);
            if (auto* p = findFirstWithin (doc.get(), "MultiSampler", "MultiSamplePart")) { t.part = std::make_unique<Xml> (*p); break; }
        }
    }
    return t;
}

void setFileRef (Xml* sampleRef, const juce::File& wav, juce::int64 frames, int rate)
{
    if (sampleRef == nullptr) return;
    if (auto* fr = sampleRef->getChildByName ("FileRef"))
    {
        setValue (fr->getChildByName ("RelativePathType"), "0");
        setValue (fr->getChildByName ("RelativePath"), juce::String());
        setValue (fr->getChildByName ("Path"), wav.getFullPathName());
        setValue (fr->getChildByName ("LivePackName"), juce::String());
        setValue (fr->getChildByName ("LivePackId"), juce::String());
        setValue (fr->getChildByName ("OriginalFileSize"), juce::String (wav.getSize()));
        setValue (fr->getChildByName ("OriginalCrc"), "0");
    }
    setValue (sampleRef->getChildByName ("LastModDate"), juce::String (wav.getLastModificationTime().toMilliseconds() / 1000));
    if (auto* sc = sampleRef->getChildByName ("SourceContext")) sc->deleteAllChildElements();
    setValue (sampleRef->getChildByName ("DefaultDuration"), juce::String (frames));
    setValue (sampleRef->getChildByName ("DefaultSampleRate"), juce::String (rate));
}

// a parameter's "Manual" value within its MidiControllerRange
void setParam (Xml* param, double v)
{
    if (param == nullptr) return;
    if (auto* r = param->getChildByName ("MidiControllerRange"))
    {
        const double lo = child (r, { "Min" }) != nullptr ? child (r, { "Min" })->getDoubleAttribute ("Value") : v;
        const double hi = child (r, { "Max" }) != nullptr ? child (r, { "Max" })->getDoubleAttribute ("Value") : v;
        v = juce::jlimit (lo, juce::jmax (lo, hi), v);
    }
    setValue (param->getChildByName ("Manual"), num (v));
}

// Sampler with the zones of a GM SoundFont preset; the waves go to <sampleDir>/<preset>/ (shared by tracks with the same sound)
std::unique_ptr<Xml> makeSampler (const Templates& tp, const gm::Preset& preset, const juce::File& sampleDir, int medianKey, int& nextPointee)
{
    auto dev = std::make_unique<Xml> (*tp.sampler);
    dev->setAttribute ("Id", 0);
    renumber (*dev, nextPointee);
    auto* parts = child (dev.get(), { "Player", "MultiSampleMap", "SampleParts" });
    if (parts == nullptr) return nullptr;
    parts->deleteAllChildElements();
    const auto dir = sampleDir.getChildFile (juce::File::createLegalFileName (preset.name.isNotEmpty() ? preset.name : juce::String ("GM")));
    dir.createDirectory();
    std::vector<juce::File> files;
    for (auto& w : preset.waves)
    {
        auto f = dir.getChildFile (juce::File::createLegalFileName (preset.name + " " + juce::MidiMessage::getMidiNoteName (w.root, true, true, 4)
                                                                    + " " + juce::String::toHexString ((int) w.id)) + ".wav");
        if (! f.existsAsFile()) gm::writeWav (w, f);
        files.push_back (f);
    }
    int id = 0;
    for (auto& z : preset.zones)
    {
        const auto& w = preset.waves[(size_t) z.wave];
        const int len = (int) w.pcm.size();
        auto part = std::make_unique<Xml> (*tp.part);
        part->setAttribute ("Id", id);
        renumber (*part, nextPointee);
        setValue (part->getChildByName ("Name"), files[(size_t) z.wave].getFileNameWithoutExtension());
        setValue (part->getChildByName ("Selection"), id == 0 ? "true" : "false");
        setValue (part->getChildByName ("IsActive"), "true");
        if (auto* kr = part->getChildByName ("KeyRange"))
        {
            setValue (kr->getChildByName ("Min"), juce::String (z.loKey)); setValue (kr->getChildByName ("Max"), juce::String (z.hiKey));
            setValue (kr->getChildByName ("CrossfadeMin"), juce::String (z.loKey)); setValue (kr->getChildByName ("CrossfadeMax"), juce::String (z.hiKey));
        }
        if (auto* vr = part->getChildByName ("VelocityRange"))
        {
            const int lo = juce::jmax (1, z.loVel), hi = juce::jmax (lo, z.hiVel);
            setValue (vr->getChildByName ("Min"), juce::String (lo)); setValue (vr->getChildByName ("Max"), juce::String (hi));
            setValue (vr->getChildByName ("CrossfadeMin"), juce::String (lo)); setValue (vr->getChildByName ("CrossfadeMax"), juce::String (hi));
        }
        setValue (part->getChildByName ("RootKey"), juce::String (z.rootKey));
        setValue (part->getChildByName ("Detune"), juce::String (juce::roundToInt (z.detuneCents)));
        setValue (part->getChildByName ("Panorama"), num (z.pan));
        setValue (part->getChildByName ("Volume"), num (juce::jlimit (0.0, 1.0, std::pow (10.0, z.gainDb / 20.0))));
        setValue (part->getChildByName ("SampleStart"), "0");
        setValue (part->getChildByName ("SampleEnd"), juce::String (len - 1));
        if (auto* sl = part->getChildByName ("SustainLoop"))
        {
            setValue (sl->getChildByName ("Start"), juce::String (z.loop ? z.loopStart : 0));
            setValue (sl->getChildByName ("End"), juce::String (z.loop ? z.loopEnd : len - 1));
            setValue (sl->getChildByName ("Mode"), z.loop ? "1" : "0");
            setValue (sl->getChildByName ("Crossfade"), "0");
            setValue (sl->getChildByName ("Detune"), "0");
        }
        if (auto* rl = part->getChildByName ("ReleaseLoop"))
        {
            setValue (rl->getChildByName ("Start"), "0");
            setValue (rl->getChildByName ("End"), juce::String (len - 1));
            setValue (rl->getChildByName ("Crossfade"), "0");
        }
        setFileRef (part->getChildByName ("SampleRef"), files[(size_t) z.wave], len, w.rate);
        if (auto* wp = part->getChildByName ("SampleWarpProperties"))
        {
            setValue (wp->getChildByName ("IsWarped"), "false");
            if (auto* wm = wp->getChildByName ("WarpMarkers")) wm->deleteAllChildElements();
        }
        if (auto* sp = part->getChildByName ("SlicePoints")) sp->deleteAllChildElements();
        if (auto* sp = part->getChildByName ("ManualSlicePoints")) sp->deleteAllChildElements();
        if (auto* sp = part->getChildByName ("BeatSlicePoints")) sp->deleteAllChildElements();
        if (auto* sp = part->getChildByName ("RegionSlicePoints")) sp->deleteAllChildElements();
        parts->addChildElement (part.release());
        ++id;
    }
    // amplitude envelope of the zone that plays the track's middle key
    const gm::Zone* ez = &preset.zones.front();
    for (auto& z : preset.zones) if (medianKey >= z.loKey && medianKey <= z.hiKey && z.hiVel >= 100) { ez = &z; break; }
    if (auto* env = child (dev.get(), { "VolumeAndPan", "Envelope" }))
    {
        setParam (env->getChildByName ("AttackTime"), juce::jmax (0.1, ez->attack * 1000.0));
        setParam (env->getChildByName ("DecayTime"), juce::jmax (1.0, ez->decay * 1000.0));
        setParam (env->getChildByName ("SustainLevel"), juce::jmax (0.0003162277571, (double) ez->sustain));
        setParam (env->getChildByName ("ReleaseTime"), juce::jmax (1.0, ez->release * 1000.0));
    }
    setValue (findFirst (dev.get(), "PitchBendRange"), "2");   // General MIDI default
    setValue (dev->getChildByName ("UserName"), preset.name);
    return dev;
}

void setClipRange (Xml& clip, double endBeat, const juce::String& name, int colour)
{
    clip.setAttribute ("Time", "0");
    setValue (clip.getChildByName ("CurrentStart"), "0");
    setValue (clip.getChildByName ("CurrentEnd"), num (endBeat));
    if (auto* loop = clip.getChildByName ("Loop"))
    {
        setValue (loop->getChildByName ("LoopStart"), "0");
        setValue (loop->getChildByName ("LoopEnd"), num (endBeat));
        setValue (loop->getChildByName ("StartRelative"), "0");
        setValue (loop->getChildByName ("LoopOn"), "false");
        setValue (loop->getChildByName ("OutMarker"), num (endBeat));
        setValue (loop->getChildByName ("HiddenLoopStart"), "0");
        setValue (loop->getChildByName ("HiddenLoopEnd"), num (endBeat));
    }
    setValue (clip.getChildByName ("Name"), name);
    setValue (clip.getChildByName ("Color"), juce::String (colour));
    if (auto* sp = clip.getChildByName ("ScrollerTimePreserver")) { setValue (sp->getChildByName ("LeftTime"), "0"); setValue (sp->getChildByName ("RightTime"), num (endBeat)); }
    if (auto* ts = clip.getChildByName ("TimeSelection")) { setValue (ts->getChildByName ("AnchorTime"), "0"); setValue (ts->getChildByName ("OtherTime"), "0"); }
    if (auto* env = child (&clip, { "Envelopes", "Envelopes" })) env->deleteAllChildElements();
}

void setTrackName (Xml& track, const juce::String& name, int colour)
{
    if (auto* n = track.getChildByName ("Name"))
    {
        setValue (n->getChildByName ("EffectiveName"), name);
        setValue (n->getChildByName ("UserName"), name);
    }
    setValue (track.getChildByName ("Color"), juce::String (colour));
}

Xml* envelopeFor (Xml* envelopes, int pointee)
{
    if (envelopes != nullptr)
        for (auto* env : envelopes->getChildIterator())
            if (auto* pid = child (env, { "EnvelopeTarget", "PointeeId" }); pid != nullptr && pid->getIntAttribute ("Value") == pointee)
                return child (env, { "Automation", "Events" });
    return nullptr;
}
} // namespace

bool abletonInstalled()
{
    return findAbletonResources() != juce::File();
}

bool writeAbleton (const juce::MemoryBlock& smf, const MidiInfo& info, juce::MemoryBlock& out, juce::String& error, const juce::File& sampleDir)
{
    const auto& tp = templates();
    if (tp.resources == juce::File()) { error = T ("Ableton Live nicht gefunden – der Live-Set-Export braucht die Vorlagen einer installierten Live-Version"); return false; }
    if (tp.set == nullptr || tp.midiClip == nullptr) { error = T ("Vorlagen der Live-Installation unvollständig (%1)", tp.resources.getFullPathName()); return false; }

    juce::MemoryInputStream in (smf, false);
    juce::MidiFile mf;
    if (! mf.readFrom (in, true)) { error = T ("MIDI-Daten nicht lesbar"); return false; }
    const double ppq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 480.0;
    std::vector<juce::MidiMessageSequence> seqs;
    {
        const bool smpte = mf.getTimeFormat() <= 0;   // SMPTE timing: as 120 BPM (like the DAWproject / REAPER writers)
        if (smpte) mf.convertTimestampTicksToSeconds();
        for (int t = 0; t < mf.getNumTracks(); ++t)
        {
            seqs.emplace_back (*mf.getTrack (t));
            if (smpte)
                for (auto* ev : seqs.back()) ev->message.setTimeStamp (ev->message.getTimeStamp() * 960.0);
        }
    }

    auto set = std::make_unique<Xml> (*tp.set);
    auto* liveSet = set->getChildByName ("LiveSet");
    auto* tracks = liveSet != nullptr ? liveSet->getChildByName ("Tracks") : nullptr;
    if (tracks == nullptr) { error = T ("Vorlagen der Live-Installation unvollständig (%1)", tp.resources.getFullPathName()); return false; }
    std::unique_ptr<Xml> midiTrackT;
    for (auto* t : tracks->getChildIterator())
        if (t->hasTagName ("MidiTrack")) { midiTrackT = std::make_unique<Xml> (*t); break; }
    if (midiTrackT == nullptr) { error = T ("Vorlagen der Live-Installation unvollständig (%1)", tp.resources.getFullPathName()); return false; }
    for (int i = tracks->getNumChildElements(); --i >= 0;)
        if (tracks->getChildElement (i)->hasTagName ("MidiTrack") || tracks->getChildElement (i)->hasTagName ("AudioTrack"))
            tracks->removeChildElement (tracks->getChildElement (i), true);
    if (auto* d = child (midiTrackT.get(), { "DeviceChain", "DeviceChain", "Devices" })) d->deleteAllChildElements();
    if (auto* e = child (midiTrackT.get(), { "DeviceChain", "MainSequencer", "ClipTimeable", "ArrangerAutomation", "Events" })) e->deleteAllChildElements();
    if (auto* e = child (midiTrackT.get(), { "AutomationEnvelopes", "Envelopes" })) e->deleteAllChildElements();

    // GM placeholder instruments: the original waves of the SoundFont the preview uses
    const auto sf2 = sampleDir != juce::File() && tp.sampler != nullptr && tp.part != nullptr ? gm::findSoundFont() : juce::File();
    int nextPointee = liveSet->getChildByName ("NextPointeeId") != nullptr ? liveSet->getChildByName ("NextPointeeId")->getIntAttribute ("Value") + 100 : 50000;
    int nextTrackId = 1000, insertAt = 0;

    // ---- song: tempo, metre, markers, end
    std::vector<std::pair<double, double>> tempos;          // beat, bpm
    std::vector<std::tuple<double, int, int>> metres;       // beat, num, den
    std::vector<std::pair<double, juce::String>> markers;
    double endBeat = 4.0;
    for (auto& sq : seqs)
        for (auto* ev : sq)
        {
            const auto& m = ev->message;
            const double beat = m.getTimeStamp() / ppq;
            endBeat = juce::jmax (endBeat, beat);
            if (m.isTempoMetaEvent()) tempos.push_back ({ beat, 60.0 / juce::jmax (1.0e-6, m.getTempoSecondsPerQuarterNote()) });
            else if (m.isTimeSignatureMetaEvent()) { int n = 4, d = 4; m.getTimeSignatureInfo (n, d); metres.push_back ({ beat, n, d }); }
            else if (m.isTextMetaEvent() && m.getMetaEventType() == 6 && m.getTextFromTextMetaEvent().trim().isNotEmpty())
                markers.push_back ({ beat, m.getTextFromTextMetaEvent().trim() });
        }
    std::stable_sort (tempos.begin(), tempos.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    std::stable_sort (metres.begin(), metres.end(), [] (auto& a, auto& b) { return std::get<0> (a) < std::get<0> (b); });
    if (tempos.empty() || tempos.front().first > 0) tempos.insert (tempos.begin(), { 0.0, tempos.empty() ? 120.0 : tempos.front().second });
    if (metres.empty() || std::get<0> (metres.front()) > 0) metres.insert (metres.begin(), { 0.0, 4, 4 });
    {
        const auto& [mb, mn, md] = metres.back();
        const double bar = 4.0 * mn / juce::jmax (1, md);
        endBeat = mb + std::ceil ((endBeat - mb) / bar - 1e-9) * bar;
    }

    // ---- one track per MIDI track and channel
    int partIndex = 0, noteCount = 0, instruments = 0;
    juce::StringArray drumTracks;   // 1-based track numbers on MIDI channel 10
    for (auto& seq : seqs)
    {
        seq.updateMatchedPairs();
        juce::String trackName;
        std::map<int, std::vector<int>> byCh;   // channel -> event indices
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            const auto& m = seq.getEventPointer (i)->message;
            if (m.isTrackNameEvent() && trackName.isEmpty()) trackName = m.getTextFromTextMetaEvent().trim();
            else if (! m.isMetaEvent() && ! m.isSysEx() && m.getChannel() > 0) byCh[m.getChannel() - 1].push_back (i);
        }
        for (auto& [ch, idx] : byCh)
        {
            int program = -1, volume = -1, pan = -1;
            bool anyNote = false;
            std::set<int> keys;
            std::vector<int> keyList;
            for (int i : idx)
            {
                const auto& m = seq.getEventPointer (i)->message;
                anyNote |= m.isNoteOn();
                if (m.isNoteOn()) { keys.insert (m.getNoteNumber()); keyList.push_back (m.getNoteNumber()); }
                if (m.isProgramChange() && program < 0) program = m.getProgramChangeNumber();
                if (m.isController() && m.getControllerNumber() == 7 && volume < 0) volume = m.getControllerValue();
                if (m.isController() && m.getControllerNumber() == 10 && pan < 0) pan = m.getControllerValue();
            }
            if (! anyNote) continue;
            const bool drums = ch == 9;
            auto name = trackName.isNotEmpty() ? trackName : (drums ? T ("Schlagzeug") : (program >= 0 ? gmProgramName (program) : T ("Spur %1", partIndex + 1)));
            if (byCh.size() > 1 && trackName.isNotEmpty()) name << " (" << (ch + 1) << ")";
            if (drums)
            {
                // Live knows no MIDI channels: the name marks the drum track (and Midi-Fundus reads it back that way)
                bool named = false;
                for (auto* k : { "drum", "schlagzeug", "batterie", "percussion" }) named |= name.toLowerCase().contains (k);
                if (! named) name << " – " << T ("Schlagzeug");
            }
            const int colour = kLiveColours[partIndex % 8];
            ++partIndex;
            if (drums) drumTracks.add (juce::String (partIndex));

            auto track = std::make_unique<Xml> (*midiTrackT);
            track->setAttribute ("Id", nextTrackId++);
            renumber (*track, nextPointee);
            setTrackName (*track, name, colour);
            if (sf2 != juce::File())
            {
                gm::Preset preset;
                juce::String why;
                if (gm::extract (sf2, drums ? 128 : 0, juce::jmax (0, program), keys, preset, why))
                {
                    std::nth_element (keyList.begin(), keyList.begin() + (long) keyList.size() / 2, keyList.end());
                    if (auto dev = makeSampler (tp, preset, sampleDir, keyList[keyList.size() / 2], nextPointee))
                        if (auto* devices = child (track.get(), { "DeviceChain", "DeviceChain", "Devices" }))
                        {
                            devices->addChildElement (dev.release());
                            ++instruments;
                        }
                }
            }
            if (auto* mx = child (track.get(), { "DeviceChain", "Mixer" }))
            {
                if (volume >= 0) setValue (child (mx, { "Volume", "Manual" }), num (std::pow (volume / 127.0, 2.0)));
                if (pan >= 0) setValue (child (mx, { "Pan", "Manual" }), num (juce::jlimit (-1.0, 1.0, (pan - 64) / 63.0)));
            }

            auto clip = std::make_unique<Xml> (*tp.midiClip);
            setClipRange (*clip, endBeat, name, colour);
            setValue (clip->getChildByName ("ProgramChange"), juce::String (drums ? -1 : program));
            setValue (clip->getChildByName ("BankSelectCoarse"), "-1");
            setValue (clip->getChildByName ("BankSelectFine"), "-1");
            if (auto* rts = findFirst (child (clip.get(), { "TimeSignature" }), "RemoteableTimeSignature"))
            {
                setValue (rts->getChildByName ("Numerator"), juce::String (std::get<1> (metres.front())));
                setValue (rts->getChildByName ("Denominator"), juce::String (std::get<2> (metres.front())));
                setValue (rts->getChildByName ("Time"), "0");
            }
            std::map<int, std::vector<std::tuple<double, double, int>>> byKey;
            Xml bendEnv ("Events");
            int bendEvents = 0, noteId = 1;
            for (int i : idx)
            {
                const auto& m = seq.getEventPointer (i)->message;
                const double beat = m.getTimeStamp() / ppq;
                if (m.isNoteOn())
                {
                    double off = beat + 0.25;
                    if (auto* o = seq.getEventPointer (i)->noteOffObject) off = o->message.getTimeStamp() / ppq;
                    byKey[m.getNoteNumber()].push_back ({ beat, juce::jmax (1.0 / 128.0, off - beat), m.getVelocity() });
                    ++noteCount;
                }
                else if (m.isPitchWheel())
                {
                    if (bendEvents == 0)
                    {
                        auto* e0 = bendEnv.createNewChildElement ("FloatEvent");
                        e0->setAttribute ("Id", 1); e0->setAttribute ("Time", "-63072000"); e0->setAttribute ("Value", "0");
                    }
                    auto* e = bendEnv.createNewChildElement ("FloatEvent");
                    e->setAttribute ("Id", bendEvents + 2);
                    e->setAttribute ("Time", num (beat));
                    e->setAttribute ("Value", juce::String (m.getPitchWheelValue() - 8192));
                    ++bendEvents;
                }
            }
            if (auto* keyTracks = child (clip.get(), { "Notes", "KeyTracks" }))
            {
                keyTracks->deleteAllChildElements();
                int kid = 0;
                for (auto& [key, notes] : byKey)
                {
                    auto* kt = keyTracks->createNewChildElement ("KeyTrack");
                    kt->setAttribute ("Id", kid++);
                    auto* ns = kt->createNewChildElement ("Notes");
                    for (auto& [time, dur, vel] : notes)
                    {
                        auto* n = ns->createNewChildElement ("MidiNoteEvent");
                        n->setAttribute ("Time", num (time));
                        n->setAttribute ("Duration", num (dur));
                        n->setAttribute ("Velocity", juce::String (vel));
                        n->setAttribute ("OffVelocity", "64");
                        n->setAttribute ("NoteId", noteId++);
                    }
                    kt->createNewChildElement ("MidiKey")->setAttribute ("Value", key);
                }
            }
            if (auto* evl = child (clip.get(), { "Notes", "PerNoteEventStore", "EventLists" })) evl->deleteAllChildElements();
            if (auto* gen = findFirst (child (clip.get(), { "Notes" }), "NoteIdGenerator")) setValue (gen->getChildByName ("NextId"), juce::String (noteId));
            if (bendEvents > 0)
                if (auto* envs = child (clip.get(), { "Envelopes", "Envelopes" }))
                    if (auto* pbTarget = child (track.get(), { "DeviceChain", "MainSequencer", "MidiControllers", "ControllerTargets.0" }))
                    {
                        auto* ce = envs->createNewChildElement ("ClipEnvelope");
                        ce->setAttribute ("Id", 0);
                        ce->createNewChildElement ("EnvelopeTarget")->createNewChildElement ("PointeeId")->setAttribute ("Value", pbTarget->getIntAttribute ("Id"));
                        auto* au = ce->createNewChildElement ("Automation");
                        au->addChildElement (new Xml (bendEnv));
                        auto* tv = au->createNewChildElement ("AutomationTransformViewState");
                        tv->createNewChildElement ("IsTransformPending")->setAttribute ("Value", "false");
                        tv->createNewChildElement ("TimeAndValueTransforms");
                        ce->createNewChildElement ("LoopSlot")->createNewChildElement ("Value");
                        auto* sp = ce->createNewChildElement ("ScrollerTimePreserver");
                        sp->createNewChildElement ("LeftTime")->setAttribute ("Value", "0");
                        sp->createNewChildElement ("RightTime")->setAttribute ("Value", "0");
                    }
            if (auto* evs = child (track.get(), { "DeviceChain", "MainSequencer", "ClipTimeable", "ArrangerAutomation", "Events" }))
                evs->addChildElement (clip.release());
            tracks->insertChildElement (track.release(), insertAt++);
        }
    }
    if (partIndex == 0) { error = T ("Keine Noten gefunden"); return false; }

    // ---- tempo and metre (first value + changes as automation of the main track)
    if (auto* main = liveSet->getChildByName ("MainTrack"))
    {
        auto* envs = child (main, { "AutomationEnvelopes", "Envelopes" });
        if (auto* tempo = child (main, { "DeviceChain", "Mixer", "Tempo" }))
        {
            setValue (tempo->getChildByName ("Manual"), num (std::round (tempos.front().second * 1000.0) / 1000.0));
            if (auto* evs = envelopeFor (envs, tempo->getChildByName ("AutomationTarget") != nullptr ? tempo->getChildByName ("AutomationTarget")->getIntAttribute ("Id") : -1))
            {
                evs->deleteAllChildElements();
                int id = 0;
                auto add = [&] (const juce::String& time, double bpm) {
                    auto* e = evs->createNewChildElement ("FloatEvent");
                    e->setAttribute ("Id", id++); e->setAttribute ("Time", time); e->setAttribute ("Value", num (bpm));
                };
                add ("-63072000", tempos.front().second);
                for (size_t k = 1; k < tempos.size(); ++k)
                {
                    add (num (tempos[k].first), tempos[k - 1].second);   // hold the previous tempo up to the change (step, not ramp)
                    add (num (tempos[k].first), tempos[k].second);
                }
            }
        }
        if (auto* sig = child (main, { "DeviceChain", "Mixer", "TimeSignature" }))
        {
            setValue (sig->getChildByName ("Manual"), juce::String (liveMetre (std::get<1> (metres.front()), std::get<2> (metres.front()))));
            if (auto* evs = envelopeFor (envs, sig->getChildByName ("AutomationTarget") != nullptr ? sig->getChildByName ("AutomationTarget")->getIntAttribute ("Id") : -1))
            {
                evs->deleteAllChildElements();
                int id = 0;
                auto add = [&] (const juce::String& time, int n, int d) {
                    auto* e = evs->createNewChildElement ("EnumEvent");
                    e->setAttribute ("Id", id++); e->setAttribute ("Time", time); e->setAttribute ("Value", liveMetre (n, d));
                };
                add ("-63072000", std::get<1> (metres.front()), std::get<2> (metres.front()));
                for (size_t k = 1; k < metres.size(); ++k) add (num (std::get<0> (metres[k])), std::get<1> (metres[k]), std::get<2> (metres[k]));
            }
        }
    }
    // ---- markers as locators
    if (auto* locs = child (liveSet, { "Locators", "Locators" }))
    {
        locs->deleteAllChildElements();
        int id = 0;
        for (auto& [beat, text] : markers)
        {
            auto* l = locs->createNewChildElement ("Locator");
            l->setAttribute ("Id", id++);
            l->createNewChildElement ("LomId")->setAttribute ("Value", "0");
            l->createNewChildElement ("Time")->setAttribute ("Value", num (beat));
            l->createNewChildElement ("Name")->setAttribute ("Value", text);
            l->createNewChildElement ("Annotation")->setAttribute ("Value", "");
            l->createNewChildElement ("IsSongStart")->setAttribute ("Value", "false");
        }
    }
    // title / artist in the set's info text
    {
        juce::StringArray ann;
        if (info.title.isNotEmpty()) ann.add ("Title: " + info.title);
        if (info.artist.isNotEmpty()) ann.add ("Artist: " + info.artist);
        if (info.composer.isNotEmpty()) ann.add ("Composer: " + info.composer);
        if (drumTracks.size() > 0) ann.add ("Drums: " + drumTracks.joinIntoString (","));
        if (ann.size() > 0) setValue (liveSet->getChildByName ("Annotation"), ann.joinIntoString ("\n"));
    }
    setValue (liveSet->getChildByName ("NextPointeeId"), juce::String (nextPointee + 10));
    juce::ignoreUnused (noteCount, instruments);

    // ---- gzip'ed XML
    out.reset();
    {
        juce::MemoryOutputStream mos (out, false);
        juce::GZIPCompressorOutputStream gz (mos, 6, juce::GZIPCompressorOutputStream::windowBitsGZIP);
        juce::XmlElement::TextFormat fmt;
        fmt.addDefaultHeader = true;
        set->writeTo (gz, fmt);
        gz.flush();
    }
    return out.getSize() > 0;
}
} // namespace ma::daw
