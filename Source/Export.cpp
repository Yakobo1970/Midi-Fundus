#include "Export.h"
#include "MidiData.h"
#include "Lang.h"
#include "Formats.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include <map>
#include <set>

namespace ma
{
namespace
{
constexpr int kDiv = 12;   // grid: 12 per quarter = sixteenths (3) and eighth / sixteenth triplets (4 / 2)

struct XNote { int start, end, pitch, vel; };
struct XPart { juce::String name; int channel = 0, program = -1; bool drums = false; std::vector<XNote> notes; };
struct XSong
{
    std::vector<XPart> parts;
    double bpm = 120;
    int num = 4, den = 4, fifths = 0;
    bool minor = false;
    juce::String title, artist, composer, copyright;
    int measureLen() const { return juce::jmax (1, num * 4 * kDiv / juce::jmax (1, den)); }
};

int keyFifths (int tonic, bool minor)
{
    // tonic 0 = C … 11 = B; minor keys use the relative major
    const int major = minor ? (tonic + 3) % 12 : tonic;
    static const int fifthsOf[12] = { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };   // C Db D Eb E F F#(Gb=-6) G Ab A Bb B
    return fifthsOf[major];
}

bool buildSong (const juce::MemoryBlock& smf, const MidiInfo& info, XSong& song, juce::String& error)
{
    juce::MemoryInputStream in (smf, false);
    juce::MidiFile mf;
    if (! mf.readFrom (in, true)) { error = T ("MIDI-Daten nicht lesbar"); return false; }
    int ppq = mf.getTimeFormat();
    if (ppq <= 0) { mf.convertTimestampTicksToSeconds(); ppq = 0; }
    auto grid = [&] (double t) { return ppq > 0 ? juce::roundToInt (t * kDiv / ppq) : juce::roundToInt (t * 2.0 * kDiv); };   // SMPTE: 120 BPM
    bool tempoSet = false, sigSet = false;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        juce::String trackName;
        std::map<int, XPart> byCh;
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            const auto& m = seq.getEventPointer (i)->message;
            if (m.isTrackNameEvent() && trackName.isEmpty()) trackName = m.getTextFromTextMetaEvent().trim();
            else if (m.isTempoMetaEvent() && ! tempoSet) { song.bpm = 60.0 / juce::jmax (1.0e-6, m.getTempoSecondsPerQuarterNote()); tempoSet = true; }
            else if (m.isTimeSignatureMetaEvent() && ! sigSet) { m.getTimeSignatureInfo (song.num, song.den); sigSet = true; }
            else if (m.isProgramChange()) { auto& p = byCh[m.getChannel() - 1]; if (p.program < 0) p.program = m.getProgramChangeNumber(); }
            else if (m.isNoteOn())
            {
                const int ch = m.getChannel() - 1;
                auto& p = byCh[ch];
                p.channel = ch;
                p.drums = ch == 9;
                const double on = seq.getEventPointer (i)->message.getTimeStamp();
                double off = on + (ppq > 0 ? ppq / 4.0 : 0.125);
                if (auto* o = seq.getEventPointer (i)->noteOffObject) off = o->message.getTimeStamp();
                const int s = grid (on);
                p.notes.push_back ({ s, juce::jmax (s + 1, grid (off)), m.getNoteNumber(), m.getVelocity() });
            }
        }
        for (auto& [ch, p] : byCh)
        {
            if (p.notes.empty()) continue;
            p.name = trackName.isNotEmpty() ? trackName : (p.drums ? T ("Schlagzeug") : (p.program >= 0 ? gmProgramName (p.program) : T ("Spur %1", (int) song.parts.size() + 1)));
            if (byCh.size() > 1 && trackName.isNotEmpty()) p.name << " (" << (ch + 1) << ")";
            std::sort (p.notes.begin(), p.notes.end(), [] (const XNote& a, const XNote& b) { return a.start != b.start ? a.start < b.start : a.pitch > b.pitch; });
            song.parts.push_back (std::move (p));
        }
    }
    if (song.parts.empty()) { error = T ("Keine Noten gefunden"); return false; }
    song.title = info.title; song.artist = info.artist; song.composer = info.composer; song.copyright = info.copyright;
    int tonic = 0;
    bool minor = false;
    if (parseKeyName (info.key.removeCharacters ("?"), tonic, minor)) { song.fifths = keyFifths (tonic, minor); song.minor = minor; }
    if (song.num <= 0 || song.den <= 0) { song.num = 4; song.den = 4; }
    return true;
}

// pitch spelling according to the key signature
void spell (int pitch, int fifths, char& step, int& alter, int& octave)
{
    static const char sharpStep[12] = { 'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B' };
    static const int sharpAlt[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
    static const char flatStep[12] = { 'C', 'D', 'D', 'E', 'E', 'F', 'G', 'G', 'A', 'A', 'B', 'B' };
    static const int flatAlt[12] = { 0, -1, 0, -1, 0, 0, -1, 0, -1, 0, -1, 0 };
    const int pc = ((pitch % 12) + 12) % 12;
    step = fifths < 0 ? flatStep[pc] : sharpStep[pc];
    alter = fifths < 0 ? flatAlt[pc] : sharpAlt[pc];
    octave = pitch / 12 - 1;
}

// note values on the 12-per-quarter grid: duration, type, dots, triplet
struct NoteValue { int dur; const char* type; int dots; bool triplet; };
const NoteValue kValues[] = {
    { 96, "breve", 0, false }, { 72, "whole", 1, false }, { 48, "whole", 0, false }, { 36, "half", 1, false }, { 24, "half", 0, false },
    { 18, "quarter", 1, false }, { 12, "quarter", 0, false }, { 9, "eighth", 1, false }, { 8, "quarter", 0, true }, { 6, "eighth", 0, false },
    { 4, "eighth", 0, true }, { 3, "16th", 0, false }, { 2, "16th", 0, true }, { 1, "32nd", 0, true } };

std::vector<const NoteValue*> splitDuration (int d)
{
    std::vector<const NoteValue*> v;
    while (d > 0)
        for (auto& nv : kValues)
            if (nv.dur <= d) { v.push_back (&nv); d -= nv.dur; break; }
    return v;
}

// ---------------------------------------------------------------- MusicXML
juce::String escapeXml (const juce::String& s) { return s.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;").replace ("\"", "&quot;"); }

juce::String toMusicXml (const XSong& song)
{
    const int mLen = song.measureLen();
    int last = 0;
    for (auto& p : song.parts) for (auto& n : p.notes) last = juce::jmax (last, n.end);
    const int measures = juce::jmax (1, (last + mLen - 1) / mLen);
    juce::MemoryOutputStream all;   // appending to one juce::String copies it every time: chunks per measure
    juce::String x;
    const auto nl = juce::String ("\n");
    x << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>" << nl
      << "<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 3.1 Partwise//EN\" \"http://www.musicxml.org/dtds/partwise.dtd\">" << nl
      << "<score-partwise version=\"3.1\">" << nl;
    if (song.title.isNotEmpty()) x << "  <work><work-title>" << escapeXml (song.title) << "</work-title></work>" << nl;
    x << "  <identification>" << nl;
    if (song.composer.isNotEmpty()) x << "    <creator type=\"composer\">" << escapeXml (song.composer) << "</creator>" << nl;
    if (song.artist.isNotEmpty()) x << "    <creator type=\"lyricist\">" << escapeXml (song.artist) << "</creator>" << nl;
    if (song.copyright.isNotEmpty()) x << "    <rights>" << escapeXml (song.copyright) << "</rights>" << nl;
    x << "    <encoding><software>Midi-Fundus</software><encoding-date>" << juce::Time::getCurrentTime().formatted ("%Y-%m-%d") << "</encoding-date></encoding>" << nl
      << "  </identification>" << nl << "  <part-list>" << nl;
    for (size_t i = 0; i < song.parts.size(); ++i)
    {
        const auto& p = song.parts[i];
        const auto id = "P" + juce::String ((int) i + 1);
        x << "    <score-part id=\"" << id << "\"><part-name>" << escapeXml (p.name) << "</part-name>" << nl;
        if (p.drums)
        {
            std::set<int> keys;
            for (auto& n : p.notes) keys.insert (n.pitch);
            for (int k : keys) x << "      <score-instrument id=\"" << id << "-I" << k << "\"><instrument-name>" << k << "</instrument-name></score-instrument>" << nl;
            for (int k : keys) x << "      <midi-instrument id=\"" << id << "-I" << k << "\"><midi-channel>10</midi-channel><midi-unpitched>" << (k + 1) << "</midi-unpitched></midi-instrument>" << nl;
        }
        else
        {
            x << "      <score-instrument id=\"" << id << "-I1\"><instrument-name>" << escapeXml (p.name) << "</instrument-name></score-instrument>" << nl
              << "      <midi-instrument id=\"" << id << "-I1\"><midi-channel>" << (p.channel + 1) << "</midi-channel><midi-program>" << (juce::jmax (0, p.program) + 1) << "</midi-program></midi-instrument>" << nl;
        }
        x << "    </score-part>" << nl;
    }
    x << "  </part-list>" << nl;

    for (size_t pi = 0; pi < song.parts.size(); ++pi)
    {
        const auto& p = song.parts[pi];
        const auto id = "P" + juce::String ((int) pi + 1);
        // chords: notes with the same start and a similar end; then up to four voices per staff.
        // When all four voices are busy, the longest running chord is cut at the new start (no note is lost).
        struct Chord { int start, end; std::vector<XNote> notes; };
        std::vector<std::vector<Chord>> voices;
        std::vector<int> voiceEnd;
        auto place = [&] (Chord c) {
            size_t v = 0;
            while (v < voices.size() && voiceEnd[v] > c.start) ++v;
            if (v >= 4)
            {
                v = 0;
                for (size_t k = 1; k < voices.size(); ++k) if (voiceEnd[k] < voiceEnd[v]) v = k;
                auto& lastChord = voices[v].back();
                if (lastChord.start == c.start)
                {
                    for (auto& n : c.notes) lastChord.notes.push_back (n);
                    lastChord.end = juce::jmin (lastChord.end, c.end);
                    voiceEnd[v] = lastChord.end;
                    return;
                }
                lastChord.end = c.start;
            }
            if (v == voices.size()) { voices.emplace_back(); voiceEnd.push_back (0); }
            voices[v].push_back (c);
            voiceEnd[v] = c.end;
        };
        for (size_t i = 0; i < p.notes.size();)
        {
            std::vector<XNote> group;
            const int start = p.notes[i].start;
            while (i < p.notes.size() && p.notes[i].start == start) group.push_back (p.notes[i++]);
            std::sort (group.begin(), group.end(), [] (const XNote& a, const XNote& b) { return a.end < b.end; });
            for (size_t k = 0; k < group.size();)
            {
                Chord c { start, group[k].end, {} };
                const int tolerance = juce::jmax (3, (group[k].end - start) / 4);
                while (k < group.size() && group[k].end - c.end <= tolerance)
                {
                    bool dup = false;
                    for (auto& n : c.notes) dup |= n.pitch == group[k].pitch;
                    if (! dup) c.notes.push_back (group[k]);
                    c.end = juce::jmax (c.end, group[k].end);
                    ++k;
                }
                place (c);
            }
        }
        double avg = 0;
        for (auto& n : p.notes) avg += n.pitch;
        avg /= juce::jmax<size_t> (1, p.notes.size());
        x << "  <part id=\"" << id << "\">" << nl;
        std::vector<size_t> next (voices.size(), 0);
        for (int m = 0; m < measures; ++m)
        {
            const int ms = m * mLen, me = ms + mLen;
            x << "    <measure number=\"" << (m + 1) << "\">" << nl;
            if (m == 0)
            {
                x << "      <attributes><divisions>" << kDiv << "</divisions><key><fifths>" << song.fifths << "</fifths><mode>" << (song.minor ? "minor" : "major")
                  << "</mode></key><time><beats>" << song.num << "</beats><beat-type>" << song.den << "</beat-type></time>";
                if (p.drums) x << "<clef><sign>percussion</sign></clef>";
                else if (avg < 57) x << "<clef><sign>F</sign><line>4</line></clef>";
                else x << "<clef><sign>G</sign><line>2</line></clef>";
                x << "</attributes>" << nl;
                if (pi == 0)
                    x << "      <direction placement=\"above\"><direction-type><metronome><beat-unit>quarter</beat-unit><per-minute>"
                      << juce::roundToInt (song.bpm) << "</per-minute></metronome></direction-type><sound tempo=\"" << juce::String (song.bpm, 2) << "\"/></direction>" << nl;
            }
            if (voices.empty())
                x << "      <note><rest measure=\"yes\"/><duration>" << mLen << "</duration><voice>1</voice></note>" << nl;
            for (size_t v = 0; v < voices.size(); ++v)
            {
                if (v > 0) x << "      <backup><duration>" << mLen << "</duration></backup>" << nl;
                int pos = ms;
                auto rest = [&] (int len) {
                    for (auto* nv : splitDuration (len))
                    {
                        x << "      <note><rest/><duration>" << nv->dur << "</duration><voice>" << (v + 1) << "</voice><type>" << nv->type << "</type>";
                        for (int d = 0; d < nv->dots; ++d) x << "<dot/>";
                        if (nv->triplet) x << "<time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification>";
                        x << "</note>" << nl;
                    }
                };
                auto& list = voices[v];
                size_t i = next[v];
                while (i < list.size() && list[i].start < me)
                {
                    const auto& c = list[i];
                    const int s = juce::jmax (c.start, ms), e = juce::jmin (c.end, me);
                    if (s > pos) rest (s - pos);
                    if (e > s)
                    {
                        const auto pieces = splitDuration (e - s);
                        for (size_t k = 0; k < pieces.size(); ++k)
                        {
                            const bool tieIn = k > 0 || c.start < ms, tieOut = k + 1 < pieces.size() || c.end > me;
                            for (size_t ni = 0; ni < c.notes.size(); ++ni)
                            {
                                const auto& n = c.notes[ni];
                                char step; int alter, octave;
                                spell (n.pitch, song.fifths, step, alter, octave);
                                x << "      <note dynamics=\"" << juce::roundToInt (n.vel * 100.0 / 90.0) << "\">";
                                if (ni > 0) x << "<chord/>";
                                if (p.drums)
                                    x << "<unpitched><display-step>" << step << "</display-step><display-octave>" << octave << "</display-octave></unpitched>";
                                else
                                {
                                    x << "<pitch><step>" << step << "</step>";
                                    if (alter != 0) x << "<alter>" << alter << "</alter>";
                                    x << "<octave>" << octave << "</octave></pitch>";
                                }
                                x << "<duration>" << pieces[k]->dur << "</duration>";
                                if (tieIn) x << "<tie type=\"stop\"/>";
                                if (tieOut) x << "<tie type=\"start\"/>";
                                if (p.drums) x << "<instrument id=\"" << id << "-I" << n.pitch << "\"/>";
                                x << "<voice>" << (v + 1) << "</voice><type>" << pieces[k]->type << "</type>";
                                for (int d = 0; d < pieces[k]->dots; ++d) x << "<dot/>";
                                if (pieces[k]->triplet) x << "<time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification>";
                                if (tieIn || tieOut)
                                {
                                    x << "<notations>";
                                    if (tieIn) x << "<tied type=\"stop\"/>";
                                    if (tieOut) x << "<tied type=\"start\"/>";
                                    x << "</notations>";
                                }
                                x << "</note>" << nl;
                            }
                        }
                        pos = e;
                    }
                    if (c.end <= me) ++i;
                    else break;
                }
                next[v] = i;
                if (pos < me) rest (me - pos);
            }
            x << "    </measure>" << nl;
            all << x;
            x.clear();
        }
        x << "  </part>" << nl;
    }
    x << "</score-partwise>" << nl;
    all << x;
    return all.toUTF8();
}

// ---------------------------------------------------------------- ABC
juce::String toAbc (const XSong& song)
{
    constexpr int unit = 3;   // L:1/16 = 3 grid steps
    const int mLen = song.measureLen() / unit;
    auto keyName = [&] {
        static const char* majors[15] = { "Cb", "Gb", "Db", "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "C#" };
        static const char* minors[15] = { "Abm", "Ebm", "Bbm", "Fm", "Cm", "Gm", "Dm", "Am", "Em", "Bm", "F#m", "C#m", "G#m", "D#m", "A#m" };
        const int i = juce::jlimit (0, 14, song.fifths + 7);
        return juce::String (song.minor ? minors[i] : majors[i]);
    };
    // key signature accidentals per letter C D E F G A B
    int keyAcc[7] = {};
    static const int sharpOrder[7] = { 3, 0, 4, 1, 5, 2, 6 };
    for (int i = 0; i < juce::jmin (7, std::abs (song.fifths)); ++i) keyAcc[song.fifths > 0 ? sharpOrder[i] : sharpOrder[6 - i]] = song.fifths > 0 ? 1 : -1;

    juce::String a;
    const auto nl = juce::String ("\n");
    a << "X:1" << nl << "T:" << (song.title.isNotEmpty() ? song.title : juce::String ("Midi-Fundus")) << nl;
    if (song.composer.isNotEmpty()) a << "C:" << song.composer << nl;
    if (song.artist.isNotEmpty()) a << "A:" << song.artist << nl;
    if (song.copyright.isNotEmpty()) a << "N:" << song.copyright << nl;
    a << "Z:Midi-Fundus" << nl << "M:" << song.num << "/" << song.den << nl << "L:1/16" << nl
      << "Q:1/4=" << juce::roundToInt (song.bpm) << nl;
    int vIndex = 0;
    juce::String body;
    for (auto& p : song.parts)
    {
        if (p.drums) continue;
        // highest note per start (skyline), re-quantised to sixteenths
        std::vector<XNote> line;
        for (auto& n : p.notes)
        {
            XNote q { juce::roundToInt ((double) n.start / unit), juce::roundToInt ((double) n.end / unit), n.pitch, n.vel };
            if (q.end <= q.start) q.end = q.start + 1;
            if (! line.empty() && line.back().start == q.start) { if (q.pitch > line.back().pitch) line.back() = q; continue; }
            if (! line.empty() && line.back().end > q.start) line.back().end = q.start;
            if (! line.empty() && line.back().end <= line.back().start) line.pop_back();
            line.push_back (q);
        }
        if (line.empty()) continue;
        ++vIndex;
        double avg = 0;
        for (auto& n : line) avg += n.pitch;
        avg /= (double) line.size();
        a << "V:" << vIndex << " name=\"" << p.name.replace ("\"", "'") << "\"" << (avg < 57 ? " clef=bass" : "") << nl;
        body << "[V:" << vIndex << "]";
        int pos = 0, barStart = 0, barsInLine = 0;
        int barAcc[7][10];
        auto resetBar = [&] { for (int l = 0; l < 7; ++l) for (int o = 0; o < 10; ++o) barAcc[l][o] = keyAcc[l]; };
        resetBar();
        auto lenStr = [] (int u) { return u == 1 ? juce::String() : juce::String (u); };
        auto emitPieces = [&] (int len, const std::function<void (int, bool)>& emitOne) {
            static const int ok[] = { 32, 24, 16, 12, 8, 6, 4, 3, 2, 1 };
            while (len > 0)
                for (int v : ok)
                    if (v <= len) { len -= v; emitOne (v, len > 0); break; }
        };
        auto advanceTo = [&] (int target, const XNote* note) {
            while (pos < target)
            {
                const int barEnd = barStart + mLen;
                const int segEnd = juce::jmin (target, barEnd);
                const int seg = segEnd - pos;
                if (note == nullptr) emitPieces (seg, [&] (int u, bool) { body << "z" << lenStr (u); });
                else
                {
                    char step; int alter, octave;
                    spell (note->pitch, song.fifths, step, alter, octave);
                    const int li = juce::String ("CDEFGAB").indexOfChar (step);
                    const int oi = juce::jlimit (0, 9, octave);
                    juce::String acc;
                    if (barAcc[li][oi] != alter) { acc = alter > 0 ? "^" : (alter < 0 ? "_" : "="); barAcc[li][oi] = alter; }
                    juce::String letter = juce::String::charToString (step);
                    if (octave >= 5) { letter = letter.toLowerCase(); for (int o = 5; o < octave; ++o) letter << "'"; }
                    else for (int o = octave; o < 4; ++o) letter << ",";
                    const bool continues = segEnd < target;
                    emitPieces (seg, [&] (int u, bool more) { body << acc << letter << lenStr (u) << (more || continues ? "-" : ""); acc = {}; });
                }
                pos = segEnd;
                if (pos == barEnd)
                {
                    body << "|";
                    barStart = barEnd;
                    resetBar();
                    if (++barsInLine == 4) { body << nl; barsInLine = 0; }
                }
            }
        };
        for (auto& n : line)
        {
            if (n.start > pos) advanceTo (n.start, nullptr);
            if (n.start < pos) continue;
            advanceTo (n.end, &n);
        }
        if (pos > barStart) { advanceTo (barStart + mLen, nullptr); }
        body << "]" << nl;
    }
    a << "K:" << keyName() << nl << body;
    return a;
}

juce::String suggestedBaseName (const MidiInfo& info, const juce::String& sourceName)
{
    auto name = info.title.isNotEmpty() ? (info.artist.isNotEmpty() ? info.artist + " - " : juce::String()) + info.title
                                        : juce::File::createLegalFileName (sourceName).upToLastOccurrenceOf (".", false, false);
    name = juce::File::createLegalFileName (name).substring (0, 120).trim();
    return name.isNotEmpty() ? name : juce::String ("Midi-Fundus");
}
} // namespace

juce::String exportExtension (ExportFormat f)
{
    switch (f)
    {
        case ExportFormat::midi: return ".mid";
        case ExportFormat::musicXml: return ".musicxml";
        case ExportFormat::abc: return ".abc";
        case ExportFormat::dawProject: return ".dawproject";
        case ExportFormat::reaper: return ".rpp";
        case ExportFormat::ableton: return ".als";
    }
    return {};
}
juce::String exportFormatName (ExportFormat f)
{
    switch (f)
    {
        case ExportFormat::midi: return "MIDI";
        case ExportFormat::musicXml: return "MusicXML";
        case ExportFormat::abc: return "ABC";
        case ExportFormat::dawProject: return "DAWproject";
        case ExportFormat::reaper: return "REAPER";
        case ExportFormat::ableton: return "Ableton Live";
    }
    return {};
}

bool exportToMemory (const juce::MemoryBlock& data, const juce::String& sourceName, ExportFormat format,
                     juce::MemoryBlock& out, juce::String& suggestedName, juce::String& error)
{
    const auto info = analyseMidi (data);
    if (! info.ok) { error = info.error; return false; }
    juce::MemoryBlock smf;
    size_t off = 0;
    if (! extractSmf (data, smf, off)) { error = T ("MIDI-Daten nicht lesbar"); return false; }
    suggestedName = suggestedBaseName (info, sourceName) + exportExtension (format);
    if (format == ExportFormat::midi) { out = smf; return true; }
    if (format == ExportFormat::dawProject) return daw::writeDawProject (smf, info, out, error);
    if (format == ExportFormat::reaper) return daw::writeReaper (smf, info, out, error);
    if (format == ExportFormat::ableton) return daw::writeAbleton (smf, info, out, error);
    XSong song;
    if (! buildSong (smf, info, song, error)) return false;
    const auto text = format == ExportFormat::musicXml ? toMusicXml (song) : toAbc (song);
    out.reset();
    out.append (text.toRawUTF8(), strlen (text.toRawUTF8()));
    return true;
}

namespace
{
// Ableton: a project folder named after the song: <Song>/<Song>.als + <Song>/Samples/<GM instrument>/*.wav
bool writeAbletonFolder (const juce::MemoryBlock& data, const juce::File& dir, juce::File& written, juce::String& error)
{
    const auto info = analyseMidi (data);
    if (! info.ok) { error = info.error; return false; }
    juce::MemoryBlock smf, out;
    size_t off = 0;
    if (! extractSmf (data, smf, off)) { error = T ("MIDI-Daten nicht lesbar"); return false; }
    const bool created = ! dir.exists();
    if (! dir.createDirectory()) { error = T ("Datei konnte nicht geschrieben werden (schreibgeschützt?)"); return false; }
    if (! daw::writeAbleton (smf, info, out, error, dir.getChildFile ("Samples"))) { if (created) dir.deleteRecursively(); return false; }
    written = dir.getChildFile (dir.getFileName() + ".als");
    if (! written.replaceWithData (out.getData(), out.getSize())) { error = T ("Datei konnte nicht geschrieben werden (schreibgeschützt?)"); return false; }
    return true;
}
} // namespace

juce::String suggestedExportName (const juce::File& source, ExportFormat format)
{
    juce::MemoryBlock data;
    MidiInfo info;
    if (source.loadFileAsData (data)) info = analyseMidi (data);
    return suggestedBaseName (info, source.getFileName()) + exportExtension (format);
}

bool exportFile (const juce::File& source, ExportFormat format, const juce::File& destFolder, juce::File& written, juce::String& error)
{
    juce::MemoryBlock data, out;
    if (hasLockedNotes (source.getFileName())) { error = lockedNotesMessage(); return false; }
    if (! source.loadFileAsData (data)) { error = T ("Datei nicht lesbar"); return false; }
    if (format == ExportFormat::ableton)
    {
        auto dir = destFolder.getChildFile (suggestedExportName (source, format).upToLastOccurrenceOf (".", false, false));
        if (dir.exists()) dir = dir.getNonexistentSibling (true);
        return writeAbletonFolder (data, dir, written, error);
    }
    juce::String name;
    if (! exportToMemory (data, source.getFileName(), format, out, name, error)) return false;
    destFolder.createDirectory();
    written = destFolder.getChildFile (name);
    if (written.exists()) written = written.getNonexistentSibling (true);
    if (! written.replaceWithData (out.getData(), out.getSize())) { error = T ("Datei konnte nicht geschrieben werden (schreibgeschützt?)"); return false; }
    return true;
}

bool exportFileAs (const juce::File& source, ExportFormat format, const juce::File& target, juce::File& written, juce::String& error)
{
    juce::MemoryBlock data, out;
    if (hasLockedNotes (source.getFileName())) { error = lockedNotesMessage(); return false; }
    if (! source.loadFileAsData (data)) { error = T ("Datei nicht lesbar"); return false; }
    const auto file = target.withFileExtension (exportExtension (format));
    if (format == ExportFormat::ableton)
    {
        // the chosen name becomes the song folder (no extra level if the user already is inside a folder of that name)
        const auto stem = juce::File::createLegalFileName (file.getFileNameWithoutExtension());
        const auto dir = file.getParentDirectory().getFileName() == stem ? file.getParentDirectory() : file.getParentDirectory().getChildFile (stem);
        return writeAbletonFolder (data, dir, written, error);
    }
    juce::String name;
    if (! exportToMemory (data, source.getFileName(), format, out, name, error)) return false;
    file.getParentDirectory().createDirectory();
    written = file;
    if (! written.replaceWithData (out.getData(), out.getSize())) { error = T ("Datei konnte nicht geschrieben werden (schreibgeschützt?)"); return false; }
    return true;
}
} // namespace ma
