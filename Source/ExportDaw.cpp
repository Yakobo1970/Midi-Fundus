// MIDIARCHIV – "Convert to" DAW projects: DAWproject 1.0 (.dawproject) and REAPER (.rpp).
// One DAW track per MIDI track and channel, one clip over the whole song, tempo / metre changes and markers.
// Instruments are not assigned (a GM program number means nothing to a DAW track); the track name carries it.
#include "Export.h"
#include "MidiData.h"
#include "Lang.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <map>

namespace ma::daw
{
namespace
{
struct Part
{
    juce::String name;
    int channel = 0, program = -1, volume = -1, pan = -1;
    struct N { double on, off; int key, vel; };
    std::vector<N> notes;                 // ticks
    struct E { double tick; juce::uint8 b[3]; int len; };
    std::vector<E> events;                // all channel events (REAPER)
};

struct Song
{
    double ppq = 480;
    std::vector<std::pair<double, double>> tempos;     // tick, bpm
    struct Sig { double tick; int num, den; };
    std::vector<Sig> sigs;
    std::vector<std::pair<double, juce::String>> markers;
    std::vector<Part> parts;
    double endTick = 0;

    double secondsAt (double tick) const
    {
        double sec = 0, last = 0, bpm = tempos.empty() ? 120.0 : tempos.front().second;
        for (auto& [t, b] : tempos)
        {
            if (t >= tick) break;
            sec += (t - last) / ppq * 60.0 / bpm;
            last = t; bpm = b;
        }
        return sec + (tick - last) / ppq * 60.0 / bpm;
    }
};

bool readSong (const juce::MemoryBlock& smf, Song& song, juce::String& error)
{
    juce::MemoryInputStream in (smf, false);
    juce::MidiFile mf;
    if (! mf.readFrom (in, true)) { error = T ("MIDI-Daten nicht lesbar"); return false; }
    double scale = 1;
    if (mf.getTimeFormat() > 0) song.ppq = mf.getTimeFormat();
    else { mf.convertTimestampTicksToSeconds(); song.ppq = 480; scale = 960; }   // SMPTE: as 120 BPM
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        juce::String trackName;
        std::map<int, Part> byCh;
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            const auto& m = seq.getEventPointer (i)->message;
            const double tick = m.getTimeStamp() * scale;
            song.endTick = juce::jmax (song.endTick, tick);
            if (m.isTrackNameEvent() && trackName.isEmpty()) trackName = m.getTextFromTextMetaEvent().trim();
            else if (m.isTempoMetaEvent()) song.tempos.push_back ({ tick, 60.0 / juce::jmax (1.0e-6, m.getTempoSecondsPerQuarterNote()) });
            else if (m.isTimeSignatureMetaEvent()) { int n = 4, d = 4; m.getTimeSignatureInfo (n, d); song.sigs.push_back ({ tick, n, d }); }
            else if (m.isTextMetaEvent() && m.getMetaEventType() == 6) song.markers.push_back ({ tick, m.getTextFromTextMetaEvent().trim() });
            else if (! m.isMetaEvent() && ! m.isSysEx() && m.getRawDataSize() <= 3 && m.getChannel() > 0)
            {
                auto& p = byCh[m.getChannel() - 1];
                p.channel = m.getChannel() - 1;
                Part::E e { tick, { 0, 0, 0 }, m.getRawDataSize() };
                memcpy (e.b, m.getRawData(), (size_t) e.len);
                p.events.push_back (e);
                if (m.isProgramChange() && p.program < 0) p.program = m.getProgramChangeNumber();
                if (m.isController() && m.getControllerNumber() == 7 && p.volume < 0) p.volume = m.getControllerValue();
                if (m.isController() && m.getControllerNumber() == 10 && p.pan < 0) p.pan = m.getControllerValue();
                if (m.isNoteOn())
                {
                    double off = tick + song.ppq / 4;
                    if (auto* o = seq.getEventPointer (i)->noteOffObject) off = o->message.getTimeStamp() * scale;
                    p.notes.push_back ({ tick, juce::jmax (tick + 1, off), m.getNoteNumber(), m.getVelocity() });
                    song.endTick = juce::jmax (song.endTick, off);
                }
            }
        }
        for (auto& [ch, p] : byCh)
        {
            if (p.notes.empty()) continue;
            const bool drums = ch == 9;
            p.name = trackName.isNotEmpty() ? trackName : (drums ? T ("Schlagzeug") : (p.program >= 0 ? gmProgramName (p.program) : T ("Spur %1", (int) song.parts.size() + 1)));
            if (byCh.size() > 1 && trackName.isNotEmpty()) p.name << " (" << (ch + 1) << ")";
            song.parts.push_back (std::move (p));
        }
    }
    if (song.parts.empty()) { error = T ("Keine Noten gefunden"); return false; }
    auto byTick = [] (auto& a, auto& b) { return a.first < b.first; };
    std::stable_sort (song.tempos.begin(), song.tempos.end(), byTick);
    std::stable_sort (song.sigs.begin(), song.sigs.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });
    if (song.tempos.empty() || song.tempos.front().first > 0) song.tempos.insert (song.tempos.begin(), { 0.0, song.tempos.empty() ? 120.0 : song.tempos.front().second });
    if (song.sigs.empty() || song.sigs.front().tick > 0) song.sigs.insert (song.sigs.begin(), { 0.0, 4, 4 });
    // round the song up to a whole bar (in the last metre)
    const auto& ls = song.sigs.back();
    const double bar = song.ppq * 4.0 * ls.num / juce::jmax (1, ls.den);
    song.endTick = ls.tick + std::ceil ((song.endTick - ls.tick) / bar - 1e-9) * bar;
    return true;
}

juce::String num (double v, int decimals = 6) { return juce::String (v, decimals).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."); }
juce::String bpmText (double bpm) { return num (bpm, 3); }   // 60e6 / microseconds: 95.000143 -> 95

const char* const kColours[] = { "#e8615a", "#e8a25a", "#d9cf55", "#7dcf6a", "#55c6c9", "#5a8fe8", "#9a6ae8", "#d96ac2" };
} // namespace

// ================================================================================================ DAWproject
bool writeDawProject (const juce::MemoryBlock& smf, const MidiInfo& info, juce::MemoryBlock& out, juce::String& error)
{
    Song song;
    if (! readSong (smf, song, error)) return false;
    int nextId = 0;
    auto id = [&] { return "id" + juce::String (nextId++); };
    auto beats = [&] (double tick) { return num (tick / song.ppq); };

    juce::XmlElement project ("Project");
    project.setAttribute ("version", "1.0");
    auto* app = project.createNewChildElement ("Application");
    app->setAttribute ("name", "Midi-Fundus");
    app->setAttribute ("version", MA_VERSION_STRING);

    auto* transport = project.createNewChildElement ("Transport");
    auto* tempo = transport->createNewChildElement ("Tempo");
    const auto tempoId = id();
    tempo->setAttribute ("unit", "bpm"); tempo->setAttribute ("value", bpmText (song.tempos.front().second));
    tempo->setAttribute ("min", "20"); tempo->setAttribute ("max", "999");
    tempo->setAttribute ("id", tempoId); tempo->setAttribute ("name", "Tempo");
    auto* sig = transport->createNewChildElement ("TimeSignature");
    const auto sigId = id();
    sig->setAttribute ("numerator", song.sigs.front().num); sig->setAttribute ("denominator", song.sigs.front().den);
    sig->setAttribute ("id", sigId);

    auto* structure = project.createNewChildElement ("Structure");
    std::vector<juce::String> trackIds;
    auto param = [&] (juce::XmlElement* parent, const char* tag, const juce::String& value, const char* unit, const char* mn, const char* mx) {
        auto* p = parent->createNewChildElement (tag);
        if (unit != nullptr) { p->setAttribute ("max", mx); p->setAttribute ("min", mn); p->setAttribute ("unit", unit); }
        p->setAttribute ("value", value);
        p->setAttribute ("id", id());
        p->setAttribute ("name", tag);
    };
    const auto masterTrackId = id(), masterId = id();
    for (size_t i = 0; i < song.parts.size(); ++i)
    {
        auto& p = song.parts[i];
        auto* tr = structure->createNewChildElement ("Track");
        trackIds.push_back (id());
        tr->setAttribute ("contentType", "notes");
        tr->setAttribute ("loaded", "true");
        tr->setAttribute ("id", trackIds.back());
        tr->setAttribute ("name", p.name);
        tr->setAttribute ("color", kColours[i % 8]);
        if (p.program >= 0 && p.channel != 9) tr->setAttribute ("comment", "GM " + juce::String (p.program + 1) + ": " + gmProgramName (p.program));
        auto* ch = tr->createNewChildElement ("Channel");
        ch->setAttribute ("audioChannels", 2);
        ch->setAttribute ("destination", masterId);
        ch->setAttribute ("role", "regular");
        ch->setAttribute ("solo", "false");
        ch->setAttribute ("id", id());
        param (ch, "Mute", "false", nullptr, nullptr, nullptr);
        param (ch, "Pan", num (p.pan >= 0 ? p.pan / 127.0 : 0.5), "normalized", "0", "1");
        param (ch, "Volume", num (p.volume >= 0 ? std::pow (p.volume / 127.0, 2.0) : 1.0), "linear", "0", "2");
    }
    {
        auto* tr = structure->createNewChildElement ("Track");
        tr->setAttribute ("contentType", "audio notes");
        tr->setAttribute ("loaded", "true");
        tr->setAttribute ("id", masterTrackId);
        tr->setAttribute ("name", "Master");
        auto* ch = tr->createNewChildElement ("Channel");
        ch->setAttribute ("audioChannels", 2);
        ch->setAttribute ("role", "master");
        ch->setAttribute ("solo", "false");
        ch->setAttribute ("id", masterId);
        param (ch, "Mute", "false", nullptr, nullptr, nullptr);
        param (ch, "Pan", "0.5", "normalized", "0", "1");
        param (ch, "Volume", "1", "linear", "0", "2");
    }

    auto* arr = project.createNewChildElement ("Arrangement");
    arr->setAttribute ("id", id());
    auto* lanes = arr->createNewChildElement ("Lanes");
    lanes->setAttribute ("timeUnit", "beats");
    lanes->setAttribute ("id", id());
    for (size_t i = 0; i < song.parts.size(); ++i)
    {
        auto& p = song.parts[i];
        auto* tl = lanes->createNewChildElement ("Lanes");
        tl->setAttribute ("track", trackIds[i]);
        tl->setAttribute ("id", id());
        auto* clips = tl->createNewChildElement ("Clips");
        clips->setAttribute ("id", id());
        auto* clip = clips->createNewChildElement ("Clip");
        clip->setAttribute ("time", "0");
        clip->setAttribute ("duration", beats (song.endTick));
        clip->setAttribute ("playStart", "0");
        clip->setAttribute ("name", p.name);
        auto* notes = clip->createNewChildElement ("Notes");
        notes->setAttribute ("id", id());
        for (auto& n : p.notes)
        {
            auto* e = notes->createNewChildElement ("Note");
            e->setAttribute ("time", beats (n.on));
            e->setAttribute ("duration", beats (n.off - n.on));
            e->setAttribute ("channel", p.channel);
            e->setAttribute ("key", n.key);
            e->setAttribute ("vel", num (n.vel / 127.0));
            e->setAttribute ("rel", num (n.vel / 127.0));
        }
    }
    if (! song.markers.empty())
    {
        auto* mk = arr->createNewChildElement ("Markers");
        mk->setAttribute ("id", id());
        for (auto& [t, name] : song.markers)
        {
            auto* m = mk->createNewChildElement ("Marker");
            m->setAttribute ("time", beats (t));
            m->setAttribute ("name", name);
        }
    }
    if (song.tempos.size() > 1)
    {
        auto* ta = arr->createNewChildElement ("TempoAutomation");
        ta->setAttribute ("unit", "bpm");
        ta->setAttribute ("id", id());
        ta->createNewChildElement ("Target")->setAttribute ("parameter", tempoId);
        for (auto& [t, b] : song.tempos)
        {
            auto* pt = ta->createNewChildElement ("RealPoint");
            pt->setAttribute ("time", beats (t));
            pt->setAttribute ("value", bpmText (b));
            pt->setAttribute ("interpolation", "hold");
        }
    }
    if (song.sigs.size() > 1)
    {
        auto* sa = arr->createNewChildElement ("TimeSignatureAutomation");
        sa->setAttribute ("id", id());
        sa->createNewChildElement ("Target")->setAttribute ("parameter", sigId);
        for (auto& s : song.sigs)
        {
            auto* pt = sa->createNewChildElement ("TimeSignaturePoint");
            pt->setAttribute ("time", beats (s.tick));
            pt->setAttribute ("numerator", s.num);
            pt->setAttribute ("denominator", s.den);
        }
    }
    project.createNewChildElement ("Scenes");

    juce::XmlElement meta ("MetaData");
    auto field = [&] (const char* tag, const juce::String& v) { if (v.isNotEmpty()) meta.createNewChildElement (tag)->addTextElement (v); };
    field ("Title", info.title); field ("Artist", info.artist); field ("Composer", info.composer);
    field ("Year", info.year > 0 ? juce::String (info.year) : juce::String());
    field ("Genre", info.genre); field ("Copyright", info.copyright); field ("Comment", info.comment);

    auto fmt = juce::XmlElement::TextFormat();
    fmt.lineWrapLength = 100000;
    const auto projectText = project.toString (fmt), metaText = meta.toString (fmt);
    juce::ZipFile::Builder zip;
    const auto now = juce::Time::getCurrentTime();
    zip.addEntry (new juce::MemoryInputStream (projectText.toRawUTF8(), strlen (projectText.toRawUTF8()), true), 9, "project.xml", now);
    zip.addEntry (new juce::MemoryInputStream (metaText.toRawUTF8(), strlen (metaText.toRawUTF8()), true), 9, "metadata.xml", now);
    juce::MemoryOutputStream os (out, false);
    out.reset();
    if (! zip.writeToStream (os, nullptr)) { error = T ("Datei konnte nicht geschrieben werden (schreibgeschützt?)"); return false; }
    os.flush();
    return true;
}

// ================================================================================================ REAPER
namespace
{
juce::String rppQuote (juce::String s)
{
    s = s.replace ("\n", " ").replace ("\r", " ");
    if (! s.containsChar ('"')) return "\"" + s + "\"";
    if (! s.containsChar ('\'')) return "'" + s + "'";
    if (! s.containsChar ('`')) return "`" + s + "`";
    return "\"" + s.replace ("\"", "'") + "\"";
}
juce::String hex2 (int v) { return juce::String::toHexString (v & 0xFF).paddedLeft ('0', 2); }
} // namespace

bool writeReaper (const juce::MemoryBlock& smf, const MidiInfo& info, juce::MemoryBlock& out, juce::String& error)
{
    Song song;
    if (! readSong (smf, song, error)) return false;
    const juce::String nl ("\n");
    juce::String r;
    r << "<REAPER_PROJECT 0.1 \"6.0/Midi-Fundus\" 0" << nl;
    r << "  TEMPO " << bpmText (song.tempos.front().second) << " " << song.sigs.front().num << " " << song.sigs.front().den << nl;
    if (song.tempos.size() > 1 || song.sigs.size() > 1)
    {
        // one point per tempo or metre change; metre as numerator + denominator * 65536
        std::map<double, std::pair<double, int>> pts;
        for (auto& [t, b] : song.tempos) pts[t].first = b;
        for (auto& s : song.sigs) pts[s.tick].second = s.num + (s.den << 16);
        double bpm = song.tempos.front().second;
        r << "  <TEMPOENVEX" << nl << "    ACT 1 -1" << nl << "    VIS 1 0 1" << nl << "    DEFSHAPE 1 -1 -1" << nl;
        for (auto& [t, v] : pts)
        {
            if (v.first > 0) bpm = v.first;
            r << "    PT " << num (song.secondsAt (t)) << " " << bpmText (bpm) << " 1";
            if (v.second > 0) r << " " << v.second;
            r << nl;
        }
        r << "  >" << nl;
    }
    int mi = 1;
    for (auto& [t, name] : song.markers) r << "  MARKER " << mi++ << " " << num (song.secondsAt (t)) << " " << rppQuote (name) << " 0" << nl;
    if (info.title.isNotEmpty() || info.artist.isNotEmpty() || info.comment.isNotEmpty() || info.genre.isNotEmpty())
    {
        r << "  <RENDER_METADATA" << nl;
        if (info.title.isNotEmpty())   r << "    TAG ID3:TIT2 " << rppQuote (info.title) << nl;
        if (info.artist.isNotEmpty())  r << "    TAG ID3:TPE1 " << rppQuote (info.artist) << nl;
        if (info.composer.isNotEmpty()) r << "    TAG ID3:TCOM " << rppQuote (info.composer) << nl;
        if (info.genre.isNotEmpty())   r << "    TAG ID3:TCON " << rppQuote (info.genre) << nl;
        if (info.year > 0)             r << "    TAG ID3:TYER " << rppQuote (juce::String (info.year)) << nl;
        if (info.comment.isNotEmpty()) r << "    TAG ID3:COMM " << rppQuote (info.comment) << nl;
        r << "  >" << nl;
    }
    const double lenSec = song.secondsAt (song.endTick);
    for (auto& p : song.parts)
    {
        r << "  <TRACK" << nl << "    NAME " << rppQuote (p.name) << nl;
        r << "    <ITEM" << nl << "      POSITION 0" << nl << "      LENGTH " << num (lenSec) << nl << "      LOOP 0" << nl
          << "      NAME " << rppQuote (p.name) << nl;
        r << "      <SOURCE MIDI" << nl << "        HASDATA 1 " << (int) song.ppq << " QN" << nl;
        auto evs = p.events;
        std::stable_sort (evs.begin(), evs.end(), [] (auto& a, auto& b) { return a.tick < b.tick; });
        juce::int64 last = 0;
        for (auto& e : evs)
        {
            const auto t = (juce::int64) std::llround (e.tick);
            r << "        E " << (t - last) << " " << hex2 (e.b[0]) << " " << hex2 (e.b[1]) << " " << hex2 (e.len > 2 ? e.b[2] : 0) << nl;
            last = t;
        }
        r << "        E " << ((juce::int64) std::llround (song.endTick) - last) << " b0 7b 00" << nl;   // end of the source
        r << "      >" << nl << "    >" << nl << "  >" << nl;
    }
    r << ">" << nl;
    out.reset();
    out.append (r.toRawUTF8(), strlen (r.toRawUTF8()));
    return true;
}
} // namespace ma::daw
