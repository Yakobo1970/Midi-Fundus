// MIDIARCHIV – notation formats: MusicXML (partwise / timewise, .mxl), MuseScore (.mscx / .mscz, versions 1–4),
// ABC (.abc) and Humdrum **kern (.krn). Notes are placed on a quarter-note grid (480 ticks), ties are merged,
// grace notes skipped, repeats played once.
#include "Formats.h"
#include "SmfBuilder.h"
#include "Lang.h"
#include <functional>
#include <map>

namespace ma::fmt
{
namespace
{
constexpr int kPpq = 480;
juce::int64 ticksOf (double quarters) { return (juce::int64) std::llround (quarters * kPpq); }

// Collects notes and merges ties (a note that continues a tied note of the same pitch on the same track).
struct NoteSink
{
    SmfBuilder& b;
    struct Open { juce::int64 on, off; int track, ch, pitch, vel; bool tiedForward; };
    std::vector<Open> notes;

    void add (int track, int ch, double startQ, double lenQ, int pitch, int vel, bool tieStart)
    {
        const auto on = ticksOf (startQ), off = ticksOf (startQ + lenQ);
        if (pitch < 0 || pitch > 127 || off <= on) return;
        for (auto it = notes.rbegin(); it != notes.rend() && it - notes.rbegin() < 64; ++it)
            if (it->tiedForward && it->track == track && it->pitch == pitch && std::abs (it->off - on) <= 2)
            {
                it->off = off;
                it->tiedForward = tieStart;
                return;
            }
        notes.push_back ({ on, off, track, ch, pitch, vel, tieStart });
    }
    void flush() { for (auto& n : notes) b.note (n.track, n.ch, n.on, n.off, n.pitch, n.vel); notes.clear(); }
};

juce::String childText (juce::XmlElement* e, const char* name)
{
    if (e == nullptr) return {};
    auto* c = e->getChildByName (name);
    return c != nullptr ? c->getAllSubText().trim() : juce::String();
}

int stepToSemitone (juce::juce_wchar step)
{
    switch (juce::CharacterFunctions::toUpperCase (step))
    {
        case 'C': return 0; case 'D': return 2; case 'E': return 4; case 'F': return 5;
        case 'G': return 7; case 'A': return 9; case 'B': return 11; default: return -100;
    }
}

void appendLyric (juce::String& lyrics, const juce::String& text, const juce::String& syllabic)
{
    if (text.isEmpty()) return;
    lyrics += text;
    if (syllabic.isEmpty() || syllabic == "single" || syllabic == "end") lyrics += " ";
}

int melodicChannel (int index) { return index < 9 ? index : juce::jmin (15, index + 1); }
} // namespace

// ============================================================================================ MusicXML
bool readMusicXml (const juce::String& xmlText, ConvertedSong& out, juce::String& error)
{
    auto root = parseXml (xmlText);
    if (root == nullptr || ! (root->hasTagName ("score-partwise") || root->hasTagName ("score-timewise"))) { error = T ("Kein MusicXML"); return false; }
    out.title = childText (root->getChildByName ("work"), "work-title");
    if (out.title.isEmpty()) out.title = childText (root.get(), "movement-title");
    if (auto* id = root->getChildByName ("identification"))
    {
        for (auto* c : id->getChildWithTagNameIterator ("creator"))
        {
            const auto type = c->getStringAttribute ("type");
            if (type == "composer") out.composer = c->getAllSubText().trim();
            else if (type == "lyricist" && out.artist.isEmpty()) out.artist = c->getAllSubText().trim();
        }
        out.copyright = childText (id, "rights");
        out.comment = childText (id->getChildByName ("encoding"), "software");
    }
    struct PartInfo { juce::String name; int ch = -1, program = -1; std::map<juce::String, int> unpitched; };
    std::map<juce::String, PartInfo> parts;
    juce::StringArray order;
    if (auto* pl = root->getChildByName ("part-list"))
        for (auto* sp : pl->getChildWithTagNameIterator ("score-part"))
        {
            auto& pi = parts[sp->getStringAttribute ("id")];
            order.add (sp->getStringAttribute ("id"));
            pi.name = childText (sp, "part-name");
            for (auto* mi : sp->getChildWithTagNameIterator ("midi-instrument"))
            {
                if (pi.ch < 0 && childText (mi, "midi-channel").isNotEmpty()) pi.ch = childText (mi, "midi-channel").getIntValue() - 1;
                if (pi.program < 0 && childText (mi, "midi-program").isNotEmpty()) pi.program = childText (mi, "midi-program").getIntValue() - 1;
                if (childText (mi, "midi-unpitched").isNotEmpty()) pi.unpitched[mi->getStringAttribute ("id")] = childText (mi, "midi-unpitched").getIntValue() - 1;
            }
        }
    std::map<juce::String, std::vector<juce::XmlElement*>> measures;
    if (root->hasTagName ("score-partwise"))
    {
        for (auto* part : root->getChildWithTagNameIterator ("part"))
        {
            const auto id = part->getStringAttribute ("id");
            if (! order.contains (id)) order.add (id);
            for (auto* m : part->getChildWithTagNameIterator ("measure")) measures[id].push_back (m);
        }
    }
    else
        for (auto* m : root->getChildWithTagNameIterator ("measure"))
            for (auto* part : m->getChildWithTagNameIterator ("part"))
            {
                const auto id = part->getStringAttribute ("id");
                if (! order.contains (id)) order.add (id);
                measures[id].push_back (part);
            }

    SmfBuilder b (kPpq);
    NoteSink sink { b };
    bool tempoSet = false, sigSet = false;
    int autoCh = 0;
    for (int pIndex = 0; pIndex < order.size(); ++pIndex)
    {
        const auto& id = order[pIndex];
        auto& pi = parts[id];
        const bool drums = pi.ch == 9 || ! pi.unpitched.empty();
        const int ch = pi.ch >= 0 && pi.ch < 16 ? pi.ch : (drums ? 9 : melodicChannel (autoCh));
        if (pi.ch < 0 && ! drums) ++autoCh;
        const int track = b.addTrack (pi.name);
        if (pi.program >= 0 && ! drums) b.program (track, 0, ch, pi.program);
        double divisions = 1, pos = 0, lastStart = 0;
        int transpose = 0;
        juce::String lyrics;
        for (auto* m : measures[id])
        {
            double measureEnd = pos;
            for (auto* e : m->getChildIterator())
            {
                if (e->hasTagName ("attributes"))
                {
                    if (childText (e, "divisions").isNotEmpty()) divisions = juce::jmax (1.0, childText (e, "divisions").getDoubleValue());
                    if (auto* tr = e->getChildByName ("transpose"))
                        transpose = childText (tr, "chromatic").getIntValue() + 12 * childText (tr, "octave-change").getIntValue();
                    if (auto* t = e->getChildByName ("time"); t != nullptr && pIndex == 0 && childText (t, "beats").isNotEmpty())
                    {
                        b.timeSig (ticksOf (pos), childText (t, "beats").getIntValue(), juce::jmax (1, childText (t, "beat-type").getIntValue()));
                        sigSet = true;
                    }
                }
                else if (e->hasTagName ("direction") || e->hasTagName ("sound"))
                {
                    auto* snd = e->hasTagName ("sound") ? e : e->getChildByName ("sound");
                    if (snd != nullptr && snd->hasAttribute ("tempo") && pIndex == 0)
                    {
                        b.tempo (ticksOf (pos), snd->getDoubleAttribute ("tempo", 120));
                        tempoSet = true;
                    }
                }
                else if (e->hasTagName ("backup")) pos -= childText (e, "duration").getDoubleValue() / divisions;
                else if (e->hasTagName ("forward")) pos += childText (e, "duration").getDoubleValue() / divisions;
                else if (e->hasTagName ("note"))
                {
                    if (e->getChildByName ("grace") != nullptr || e->getChildByName ("cue") != nullptr) continue;
                    const bool chord = e->getChildByName ("chord") != nullptr;
                    const double dur = childText (e, "duration").getDoubleValue() / divisions;
                    const double start = chord ? lastStart : pos;
                    if (! chord) { lastStart = pos; pos += dur; }
                    int pitch = -1;
                    if (auto* p = e->getChildByName ("pitch"))
                    {
                        const auto step = childText (p, "step");
                        if (step.isNotEmpty())
                            pitch = 12 * (childText (p, "octave").getIntValue() + 1) + stepToSemitone (step[0])
                                    + juce::roundToInt (childText (p, "alter").getDoubleValue()) + transpose;
                    }
                    else if (auto* u = e->getChildByName ("unpitched"))
                    {
                        if (auto* ins = e->getChildByName ("instrument"); ins != nullptr && pi.unpitched.count (ins->getStringAttribute ("id")))
                            pitch = pi.unpitched[ins->getStringAttribute ("id")];
                        else if (childText (u, "display-step").isNotEmpty())
                            pitch = 12 * (childText (u, "display-octave").getIntValue() + 1) + stepToSemitone (childText (u, "display-step")[0]);
                    }
                    bool tieStart = false;
                    for (auto* t : e->getChildWithTagNameIterator ("tie")) if (t->getStringAttribute ("type") == "start") tieStart = true;
                    int vel = 90;
                    if (e->hasAttribute ("dynamics")) vel = juce::jlimit (1, 127, juce::roundToInt (e->getDoubleAttribute ("dynamics") * 0.9));
                    if (pitch >= 0 && e->getChildByName ("rest") == nullptr) sink.add (track, ch, start, dur, pitch, vel, tieStart);
                    if (auto* ly = e->getChildByName ("lyric"); ly != nullptr && ! chord)
                        appendLyric (lyrics, childText (ly, "text"), childText (ly, "syllabic"));
                }
                measureEnd = juce::jmax (measureEnd, pos);
            }
            pos = juce::jmax (pos, measureEnd);
        }
        if (out.lyrics.isEmpty() && lyrics.trim().isNotEmpty()) out.lyrics = lyrics.trim();
    }
    sink.flush();
    if (! tempoSet) b.tempo (0, 120.0);
    if (! sigSet) b.timeSig (0, 4, 4);
    if (! b.hasNotes()) { error = T ("Keine Noten gefunden"); return false; }
    out.format = "MusicXML";
    out.smf = b.build();
    return true;
}

// ============================================================================================ MuseScore
bool readMuseScore (const juce::String& xmlText, ConvertedSong& out, juce::String& error)
{
    auto root = parseXml (xmlText);
    juce::XmlElement* score = nullptr;   // MuseScore 2–4 wrap everything in <Score>, 1.x does not
    if (root != nullptr && root->hasTagName ("museScore")) score = root->getChildByName ("Score") != nullptr ? root->getChildByName ("Score") : root.get();
    if (score == nullptr) { error = T ("Keine MuseScore-Datei"); return false; }
    for (auto* mt : score->getChildWithTagNameIterator ("metaTag"))
    {
        const auto name = mt->getStringAttribute ("name"), v = mt->getAllSubText().trim();
        if (v.isEmpty()) continue;
        if (name == "workTitle" || (name == "movementTitle" && out.title.isEmpty())) out.title = v;
        else if (name == "composer") out.composer = v;
        else if (name == "lyricist") out.artist = v;
        else if (name == "copyright") out.copyright = v;
    }
    struct PartInfo { juce::String name; int program = 0; bool drums = false; int index = 0; };
    std::map<juce::String, PartInfo> staffPart;
    int partCount = 0;
    for (auto* part : score->getChildWithTagNameIterator ("Part"))
    {
        PartInfo pi;
        pi.index = partCount++;
        if (auto* ins = part->getChildByName ("Instrument"))
        {
            pi.name = childText (ins, "longName");
            if (pi.name.isEmpty()) pi.name = childText (ins, "trackName");
            pi.drums = childText (ins, "useDrumset") == "1";
            if (auto* chn = ins->getChildByName ("Channel"))
                if (auto* prog = chn->getChildByName ("program")) pi.program = prog->getIntAttribute ("value");
        }
        if (pi.name.isEmpty()) pi.name = childText (part, "trackName");
        for (auto* st : part->getChildWithTagNameIterator ("Staff")) staffPart[st->getStringAttribute ("id")] = pi;
    }
    if (out.title.isEmpty())
    {
        std::function<bool (juce::XmlElement*)> walk = [&] (juce::XmlElement* e) {
            for (auto* c : e->getChildIterator())
            {
                if (c->hasTagName ("Text") && (childText (c, "style") == "Title" || childText (c, "subtype") == "Title"))
                {
                    out.title = childText (c, "text");
                    if (out.title.isEmpty())
                    {
                        // MuseScore 1.x: rich text in <html-data><html><head><style>…</style></head><body>…</body>
                        std::function<juce::XmlElement* (juce::XmlElement*)> body = [&] (juce::XmlElement* x) -> juce::XmlElement* {
                            if (x->hasTagName ("body")) return x;
                            for (auto* y : x->getChildIterator()) if (auto* r = body (y)) return r;
                            return nullptr;
                        };
                        auto* bd = body (c);
                        out.title = (bd != nullptr ? bd->getAllSubText() : c->getAllSubText()).replace (juce::newLine, " ").replaceCharacter ('\n', ' ').trim();
                    }
                    return true;
                }
                if (! c->hasTagName ("Measure") && walk (c)) return true;
            }
            return false;
        };
        walk (score);
    }

    auto durOf = [] (const juce::String& type, int dots, double measureLen) {
        double q = type == "long" ? 16 : type == "breve" ? 8 : type == "whole" ? 4 : type == "half" ? 2 : type == "quarter" ? 1
                 : type == "eighth" ? 0.5 : type == "16th" ? 0.25 : type == "32nd" ? 0.125 : type == "64th" ? 0.0625
                 : type == "128th" ? 0.03125 : type == "256th" ? 0.015625 : type == "measure" ? measureLen : 1;
        double add = q;
        for (int i = 0; i < dots; ++i) { add *= 0.5; q += add; }
        return q;
    };
    auto fraction = [] (const juce::String& f) {
        const auto num = f.upToFirstOccurrenceOf ("/", false, false).getDoubleValue(), den = f.fromFirstOccurrenceOf ("/", false, false).getDoubleValue();
        return den > 0 ? num * 4.0 / den : 0.0;
    };
    auto isGrace = [] (juce::XmlElement* e) {
        for (auto* g : { "acciaccatura", "appoggiatura", "grace4", "grace16", "grace32", "grace8after", "grace16after", "grace32after" })
            if (e->getChildByName (g) != nullptr) return true;
        return false;
    };

    SmfBuilder b (kPpq);
    NoteSink sink { b };
    bool tempoSet = false, sigSet = false;
    int staffIndex = 0;
    std::map<int, int> partTrack;   // part index -> SMF track
    for (auto* staff : score->getChildWithTagNameIterator ("Staff"))
    {
        const auto sid = staff->getStringAttribute ("id");
        const auto pi = staffPart.count (sid) ? staffPart[sid] : PartInfo {};
        int track;
        const bool first = partTrack.count (pi.index) == 0;
        if (first) partTrack[pi.index] = b.addTrack (pi.name);
        track = partTrack[pi.index];
        const int ch = pi.drums ? 9 : melodicChannel (pi.index);
        if (first && ! pi.drums) b.program (track, 0, ch, pi.program);
        double measureStart = 0, measureLen = 4;
        juce::String lyrics;
        std::map<juce::String, double> tupletDefs;   // MuseScore 1/2: id -> normal/actual
        for (auto* m : staff->getChildWithTagNameIterator ("Measure"))
        {
            double thisLen = m->hasAttribute ("len") ? fraction (m->getStringAttribute ("len")) : measureLen;
            juce::Array<juce::XmlElement*> voices;
            for (auto* v : m->getChildWithTagNameIterator ("voice")) voices.add (v);
            if (voices.isEmpty()) voices.add (m);
            int vIndex = 0;
            for (auto* voice : voices)
            {
                double pos = measureStart, tupletRatio = 1.0;
                for (auto* e : voice->getChildIterator())
                {
                    if (e->hasTagName ("TimeSig"))
                    {
                        int num = childText (e, "sigN").getIntValue(), den = childText (e, "sigD").getIntValue();
                        if (num <= 0) { num = childText (e, "nom1").getIntValue(); den = childText (e, "den").getIntValue(); }
                        if (num > 0 && den > 0)
                        {
                            measureLen = num * 4.0 / den;
                            if (! m->hasAttribute ("len")) thisLen = measureLen;
                            if (staffIndex == 0 && vIndex == 0) { b.timeSig (ticksOf (pos), num, den); sigSet = true; }
                        }
                    }
                    else if (e->hasTagName ("Tempo"))
                    {
                        const double bps = childText (e, "tempo").getDoubleValue();
                        if (bps > 0 && staffIndex == 0) { b.tempo (ticksOf (pos), bps * 60.0); tempoSet = true; }
                    }
                    else if (e->hasTagName ("Tuplet"))
                    {
                        const double normal = childText (e, "normalNotes").getDoubleValue(), actual = childText (e, "actualNotes").getDoubleValue();
                        const double r = normal > 0 && actual > 0 ? normal / actual : 1.0;
                        if (e->hasAttribute ("id")) tupletDefs[e->getStringAttribute ("id")] = r;   // MuseScore 1/2 definition
                        else tupletRatio = r;                                                     // MuseScore 3/4: until endTuplet
                    }
                    else if (e->hasTagName ("endTuplet")) tupletRatio = 1.0;
                    else if (e->hasTagName ("location")) pos += fraction (childText (e, "fractions"));
                    else if (e->hasTagName ("Chord") || e->hasTagName ("Rest"))
                    {
                        if (isGrace (e)) continue;
                        double ratio = tupletRatio;
                        if (auto t = childText (e, "Tuplet"); t.isNotEmpty() && tupletDefs.count (t)) ratio = tupletDefs[t];
                        const auto type = childText (e, "durationType");
                        double dur = durOf (type, childText (e, "dots").getIntValue(), thisLen) * ratio;
                        if (type == "measure" && childText (e, "duration").isNotEmpty()) dur = fraction (childText (e, "duration"));
                        if (e->hasTagName ("Chord"))
                        {
                            for (auto* n : e->getChildWithTagNameIterator ("Note"))
                            {
                                bool tie = n->getChildByName ("Tie") != nullptr;
                                for (auto* sp : n->getChildWithTagNameIterator ("Spanner"))
                                    if (sp->getStringAttribute ("type") == "Tie" && sp->getChildByName ("next") != nullptr) tie = true;
                                int vel = 90;
                                if (auto* v = n->getChildByName ("velocity")) vel = juce::jlimit (1, 127, 90 + v->getAllSubText().getIntValue());
                                if (auto* p = n->getChildByName ("pitch")) sink.add (track, ch, pos, dur, p->getAllSubText().getIntValue(), vel, tie);
                            }
                            if (staffIndex == 0 && vIndex == 0)
                                if (auto* ly = e->getChildByName ("Lyrics")) appendLyric (lyrics, childText (ly, "text"), childText (ly, "syllabic"));
                        }
                        pos += dur;
                    }
                }
                ++vIndex;
            }
            measureStart += thisLen;
        }
        if (out.lyrics.isEmpty() && lyrics.trim().isNotEmpty()) out.lyrics = lyrics.trim();
        ++staffIndex;
    }
    sink.flush();
    if (! tempoSet) b.tempo (0, 120.0);
    if (! sigSet) b.timeSig (0, 4, 4);
    if (! b.hasNotes()) { error = T ("Keine Noten gefunden"); return false; }
    out.format = "MuseScore";
    out.smf = b.build();
    out.comment = "MuseScore " + root->getStringAttribute ("version");
    return true;
}

// ============================================================================================ ABC
// ABC 2.1 subset: header X/T/C/M/L/Q/K/R/N/S/Z, voices (V:), notes with accidentals and octave marks, lengths (2, /2, 3/2),
// chords [CEG], ties (-), broken rhythm (> <), tuplets ((3, (p:q:r), rests (z x Z), inline fields [K:…] [L:…] [M:…] [Q:…],
// lyrics (w:). Ignored: chord symbols "Am", decorations !…! and +…+, grace notes {…}, repeats (played once).
// Files with several tunes: the first tune is read.
bool readAbc (const juce::String& text, ConvertedSong& out, juce::String& error)
{
    juce::StringArray lines;
    lines.addLines (text);
    int tunes = 0;
    for (auto& l : lines) if (l.startsWith ("X:")) ++tunes;

    SmfBuilder b (kPpq);
    NoteSink sink { b };
    double unit = 0.125;        // L: as fraction of a whole note
    bool unitSet = false;
    double meterQ = 4.0;
    int keyAcc[7] = { 0, 0, 0, 0, 0, 0, 0 };   // C D E F G A B
    bool tempoSet = false, sigSet = false;
    struct Voice { int track = -1, ch = 0; double pos = 0; };
    std::map<juce::String, Voice> voices;
    juce::String curVoice = "1";
    juce::String lyrics;
    int tuneIndex = 0;
    bool inBody = false;

    auto voice = [&] () -> Voice& {
        auto& v = voices[curVoice];
        if (v.track < 0) { v.track = b.addTrack (voices.size() > 1 || curVoice != "1" ? "Voice " + curVoice : juce::String()); v.ch = melodicChannel ((int) voices.size() - 1); }
        return v;
    };
    auto setKey = [&] (juce::String k) {
        k = k.trim();
        for (auto& a : keyAcc) a = 0;
        if (k.isEmpty() || k.startsWithIgnoreCase ("none")) return;
        static const char* letters = "CDEFGAB";
        static const int fifthsOfMajor[7] = { 0, 2, 4, -1, 1, 3, 5 };
        int idx = juce::String (letters).indexOfChar (juce::CharacterFunctions::toUpperCase (k[0]));
        if (idx < 0) return;
        int fifths = fifthsOfMajor[idx];
        int p = 1;
        if (k[p] == '#') { fifths += 7; ++p; } else if (k[p] == 'b') { fifths -= 7; ++p; }
        const auto mode = k.substring (p).trim().toLowerCase();
        if (mode.startsWith ("m") && ! mode.startsWith ("maj") && ! mode.startsWith ("mix")) fifths -= 3;
        else if (mode.startsWith ("dor")) fifths -= 2;
        else if (mode.startsWith ("phr")) fifths -= 4;
        else if (mode.startsWith ("lyd")) fifths += 1;
        else if (mode.startsWith ("mix")) fifths -= 1;
        else if (mode.startsWith ("aeo")) fifths -= 3;
        else if (mode.startsWith ("loc")) fifths -= 5;
        static const int sharpOrder[7] = { 3, 0, 4, 1, 5, 2, 6 };   // F C G D A E B
        for (int i = 0; i < juce::jmin (7, std::abs (fifths)); ++i)
            keyAcc[fifths > 0 ? sharpOrder[i] : sharpOrder[6 - i]] = fifths > 0 ? 1 : -1;
        // explicit accidentals after the mode, e.g. "K:D exp ^f"
        for (int i = p; i < k.length(); ++i)
            if ((k[i] == '^' || k[i] == '_' || k[i] == '=') && i + 1 < k.length())
            {
                const int li = juce::String (letters).indexOfChar (juce::CharacterFunctions::toUpperCase (k[i + 1]));
                if (li >= 0) keyAcc[li] = k[i] == '^' ? 1 : (k[i] == '_' ? -1 : 0);
            }
    };
    auto setMeter = [&] (juce::String m) {
        m = m.trim();
        int num = 4, den = 4;
        if (m == "C") { num = 4; den = 4; }
        else if (m == "C|") { num = 2; den = 2; }
        else if (m.containsChar ('/')) { num = m.upToFirstOccurrenceOf ("/", false, false).getIntValue(); den = m.fromFirstOccurrenceOf ("/", false, false).getIntValue(); }
        else return;
        if (num <= 0 || den <= 0) return;
        meterQ = num * 4.0 / den;
        if (! unitSet) unit = (double) num / den < 0.75 ? 0.0625 : 0.125;
        b.timeSig (ticksOf (voices.empty() ? 0 : voice().pos), num, den);
        sigSet = true;
    };
    auto setTempo = [&] (juce::String q) {
        q = q.trim().unquoted();
        double bpm = 0, beat = 0.25;
        if (q.containsChar ('='))
        {
            const auto lhs = q.upToFirstOccurrenceOf ("=", false, false), rhs = q.fromFirstOccurrenceOf ("=", false, false);
            bpm = rhs.getDoubleValue();
            if (lhs.containsChar ('/')) beat = lhs.upToFirstOccurrenceOf ("/", false, false).getDoubleValue() / juce::jmax (1.0, lhs.fromFirstOccurrenceOf ("/", false, false).getDoubleValue());
        }
        else bpm = q.getDoubleValue();
        if (bpm > 0) { b.tempo (ticksOf (voices.empty() ? 0 : voice().pos), bpm * beat * 4.0); tempoSet = true; }
    };
    auto field = [&] (juce::juce_wchar f, const juce::String& v) {
        switch (f)
        {
            case 'T': if (out.title.isEmpty()) out.title = v.trim(); break;
            case 'C': if (out.composer.isEmpty()) out.composer = v.trim(); break;
            case 'R': if (out.genre.isEmpty()) out.genre = v.trim(); break;
            case 'N': case 'S': out.comment = (out.comment.isNotEmpty() ? out.comment + " | " : juce::String()) + v.trim(); break;
            case 'L': if (v.containsChar ('/')) { unit = v.upToFirstOccurrenceOf ("/", false, false).getDoubleValue() / juce::jmax (1.0, v.fromFirstOccurrenceOf ("/", false, false).getDoubleValue()); unitSet = true; } break;
            case 'M': setMeter (v); break;
            case 'Q': setTempo (v); break;
            case 'K': setKey (v.upToFirstOccurrenceOf (" clef", false, true)); inBody = true; break;
            case 'V': curVoice = v.trim().upToFirstOccurrenceOf (" ", false, false); if (curVoice.isEmpty()) curVoice = "1"; break;
            default: break;
        }
    };

    for (auto& rawLine : lines)
    {
        auto line = rawLine.upToFirstOccurrenceOf ("%", false, false);
        if (line.startsWith ("X:"))
        {
            if (++tuneIndex > 1) break;
            continue;
        }
        if (tuneIndex == 0 && tunes > 0) continue;
        if (line.length() >= 2 && line[1] == ':' && juce::CharacterFunctions::isLetter (line[0]) && (line[0] != '|'))
        {
            if (line[0] == 'w') { lyrics << line.substring (2).replace ("-", "").replace ("_", "").replace ("*", "").replace ("~", " ").trim() << "\n"; continue; }
            field (line[0], line.substring (2));
            continue;
        }
        if (! inBody) continue;

        // body line
        auto& v = voice();
        int barAcc[7][10] {};   // accidentals inside the bar: letter x octave (0 = none, else value+3)
        auto resetBar = [&] { for (auto& r : barAcc) for (auto& c : r) c = 0; };
        double brokenNext = 1.0;
        int tupletLeft = 0;
        double tupletRatio = 1.0;
        int lastPitch = -1;
        double lastStart = 0, lastLen = 0;
        bool tiePending = false;
        auto readLength = [&] (int& i) {
            double num = 0, den = 1;
            bool hasNum = false;
            while (i < line.length() && juce::CharacterFunctions::isDigit (line[i])) { num = num * 10 + (line[i] - '0'); ++i; hasNum = true; }
            if (! hasNum) num = 1;
            while (i < line.length() && line[i] == '/')
            {
                ++i;
                double d = 0;
                bool hasD = false;
                while (i < line.length() && juce::CharacterFunctions::isDigit (line[i])) { d = d * 10 + (line[i] - '0'); ++i; hasD = true; }
                den *= hasD ? d : 2;
            }
            return num / den;
        };
        auto readPitch = [&] (int& i, int& pitch) {
            int acc = 0;
            bool explicitAcc = false;
            while (i < line.length() && (line[i] == '^' || line[i] == '_' || line[i] == '='))
            {
                explicitAcc = true;
                if (line[i] == '^') ++acc; else if (line[i] == '_') --acc; else acc = 0;
                ++i;
            }
            if (i >= line.length()) return false;
            const auto c = line[i];
            const int li = juce::String ("CDEFGAB").indexOfChar (juce::CharacterFunctions::toUpperCase (c));
            if (li < 0) return false;
            ++i;
            int octave = juce::CharacterFunctions::isLowerCase (c) ? 5 : 4;
            while (i < line.length() && (line[i] == '\'' || line[i] == ','))
            {
                octave += line[i] == '\'' ? 1 : -1;
                ++i;
            }
            const int oi = juce::jlimit (0, 9, octave);
            if (explicitAcc) barAcc[li][oi] = acc + 3;
            const int a = barAcc[li][oi] != 0 ? barAcc[li][oi] - 3 : keyAcc[li];
            static const int semis[7] = { 0, 2, 4, 5, 7, 9, 11 };
            pitch = 12 * (octave + 1) + semis[li] + a;
            return true;
        };
        auto emit = [&] (const std::vector<int>& pitches, double lenQ) {
            double len = lenQ * brokenNext;
            brokenNext = 1.0;
            if (tupletLeft > 0) { len *= tupletRatio; --tupletLeft; }
            for (int p : pitches)
                sink.add (v.track, v.ch, v.pos, len, p, 90, false);
            lastStart = v.pos; lastLen = len;
            v.pos += len;
            return len;
        };
        int i = 0;
        while (i < line.length())
        {
            const auto c = line[i];
            if (c == '"') { const int e = line.indexOfChar (i + 1, '"'); i = e < 0 ? line.length() : e + 1; continue; }
            if (c == '!' || c == '+') { const int e = line.indexOfChar (i + 1, c); i = e < 0 ? i + 1 : e + 1; continue; }
            if (c == '{') { const int e = line.indexOfChar (i + 1, '}'); i = e < 0 ? line.length() : e + 1; continue; }
            if (c == '[' && i + 2 < line.length() && line[i + 2] == ':' && juce::CharacterFunctions::isLetter (line[i + 1]))
            {
                const int e = line.indexOfChar (i, ']');
                const auto f = line.substring (i + 1, e < 0 ? line.length() : e);
                field (f[0], f.substring (2));
                i = e < 0 ? line.length() : e + 1;
                if (f[0] == 'V') { /* voice switch inside a line */ }
                continue;
            }
            if (c == '|' || c == ':' || c == ']' || (c == '[' && i + 1 < line.length() && (juce::CharacterFunctions::isDigit (line[i + 1]) || line[i + 1] == '|')))
            {
                resetBar();
                ++i;
                while (i < line.length() && (juce::CharacterFunctions::isDigit (line[i]) || line[i] == ',' || line[i] == '-')) ++i;   // [1 [2 endings
                continue;
            }
            if (c == '(' && i + 1 < line.length() && juce::CharacterFunctions::isDigit (line[i + 1]))
            {
                ++i;
                const int p = line[i] - '0';
                ++i;
                int q = (p == 3 || p == 6) ? 2 : (p == 2 || p == 4 || p == 8) ? 3 : 2, r = p;
                if (i < line.length() && line[i] == ':') { ++i; if (i < line.length() && juce::CharacterFunctions::isDigit (line[i])) { q = line[i] - '0'; ++i; }
                    if (i < line.length() && line[i] == ':') { ++i; if (i < line.length() && juce::CharacterFunctions::isDigit (line[i])) { r = line[i] - '0'; ++i; } } }
                tupletRatio = (double) q / juce::jmax (1, p);
                tupletLeft = r;
                continue;
            }
            if (c == '>' || c == '<')
            {
                int n = 0;
                while (i < line.length() && line[i] == c) { ++n; ++i; }
                const double f = 1.0 - std::pow (0.5, n);
                // shorten / lengthen the previous note, set the factor for the next one
                const double prevFactor = c == '>' ? 1.0 + f : 1.0 - f;
                const double delta = lastLen * (prevFactor - 1.0);
                v.pos += delta;
                for (auto it = sink.notes.rbegin(); it != sink.notes.rend(); ++it)
                    if (it->track == v.track && it->on == ticksOf (lastStart)) it->off = ticksOf (lastStart + lastLen * prevFactor);
                brokenNext = c == '>' ? 1.0 - f : 1.0 + f;
                continue;
            }
            if (c == '-') { tiePending = true; for (auto it = sink.notes.rbegin(); it != sink.notes.rend(); ++it) if (it->track == v.track && it->pitch == lastPitch) { it->tiedForward = true; break; } ++i; continue; }
            if (c == 'z' || c == 'x') { ++i; emit ({}, readLength (i) * unit * 4.0); continue; }
            if (c == 'Z') { ++i; const double bars = readLength (i); v.pos += bars * meterQ; continue; }
            if (c == '[')
            {
                ++i;
                std::vector<int> ps;
                double len = -1;
                while (i < line.length() && line[i] != ']')
                {
                    int p = 0;
                    if (readPitch (i, p)) { ps.push_back (p); const double l = readLength (i); if (len < 0) len = l; }
                    else ++i;
                    while (i < line.length() && line[i] == '-') ++i;
                }
                ++i;
                const double mult = readLength (i);
                if (! ps.empty()) { emit (ps, (len < 0 ? 1 : len) * mult * unit * 4.0); lastPitch = ps.front(); }
                continue;
            }
            int pitch = 0;
            const int save = i;
            if (readPitch (i, pitch))
            {
                const double len = readLength (i) * unit * 4.0;
                tiePending = false;
                emit ({ pitch }, len);
                lastPitch = pitch;
                continue;
            }
            i = save + 1;
        }
        juce::ignoreUnused (tiePending);
    }
    sink.flush();
    if (! tempoSet) b.tempo (0, 120.0);
    if (! sigSet) b.timeSig (0, 4, 4);
    if (! b.hasNotes()) { error = T ("Keine Noten gefunden"); return false; }
    out.format = "ABC";
    out.smf = b.build();
    out.lyrics = lyrics.trim();
    if (tunes > 1) out.comment = (out.comment.isNotEmpty() ? out.comment + " | " : juce::String()) + T ("%1 Stücke in der Datei, gelesen: das erste", tunes);
    return true;
}
// ============================================================================================ Humdrum **kern
// Tab-separated spines; "**kern" columns are read, others skipped. Reference records !!!OTL (title), !!!COM (composer),
// !!!LYR (lyricist), !!!YEC / !!!YER (copyright / release), *MM (tempo), *M n/d (metre), spine split *^ and join *v.
// Tokens: duration as reciprocal (4 = quarter, 8. = dotted eighth, 0 = breve), pitch letters (c = middle C, cc above,
// C below), # / - / n accidentals, chords separated by spaces, [ ] _ ties, r rest, q grace (skipped), . null.
bool readKern (const juce::String& text, ConvertedSong& out, juce::String& error)
{
    juce::StringArray lines;
    lines.addLines (text);
    SmfBuilder b (kPpq);
    NoteSink sink { b };
    struct Spine { bool kern = false; double pos = 0; int track = -1, ch = 0; };
    std::vector<Spine> spines;
    bool tempoSet = false, sigSet = false, started = false;
    int kernCount = 0;

    auto tokenNotes = [&] (Spine& sp, const juce::String& tok) {
        if (tok == "." || tok.isEmpty()) return;
        juce::StringArray sub;
        sub.addTokens (tok, " ", "");
        double dur = -1;
        for (auto& t : sub)
        {
            if (t.containsChar ('q') || t.containsChar ('Q')) return;   // grace notes take no time
            int i = 0;
            double recip = -1;
            juce::String num;
            while (i < t.length() && juce::CharacterFunctions::isDigit (t[i])) num += t[i++];
            if (num.isNotEmpty()) recip = num.getDoubleValue();
            double q = recip == 0 ? 8.0 : (recip > 0 ? 4.0 / recip : -1);
            int dots = 0;
            for (auto ch : t) if (ch == '.') ++dots;
            double add = q;
            for (int d = 0; d < dots && q > 0; ++d) { add *= 0.5; q += add; }
            if (q > 0 && dur < 0) dur = q;
            if (t.containsChar ('r')) continue;
            // pitch
            int letter = -1, count = 0;
            bool lower = false;
            for (auto ch : t)
            {
                const int li = juce::String ("CDEFGAB").indexOfChar (juce::CharacterFunctions::toUpperCase (ch));
                if (li >= 0 && (letter < 0 || li == letter)) { letter = li; lower = juce::CharacterFunctions::isLowerCase (ch); ++count; }
            }
            if (letter < 0 || q <= 0) continue;
            static const int semis[7] = { 0, 2, 4, 5, 7, 9, 11 };
            const int octave = lower ? 3 + count : 4 - count;
            int acc = 0;
            for (auto ch : t) { if (ch == '#') ++acc; else if (ch == '-') --acc; }
            const bool tieStart = t.containsChar ('[') || t.containsChar ('_');
            sink.add (sp.track, sp.ch, sp.pos, q, 12 * (octave + 1) + semis[letter] + acc, 90, tieStart);
        }
        if (dur > 0) sp.pos += dur;
    };

    for (auto& line : lines)
    {
        if (line.startsWith ("!!!"))
        {
            const auto key = line.substring (3).upToFirstOccurrenceOf (":", false, false).trim();
            const auto v = line.fromFirstOccurrenceOf (":", false, false).trim();
            if (key == "OTL" && out.title.isEmpty()) out.title = v;
            else if (key == "COM" && out.composer.isEmpty()) out.composer = v;
            else if (key == "LYR" && out.artist.isEmpty()) out.artist = v;
            else if (key == "YEC") out.copyright = v;
            else if (key == "ODT" || key == "CDT") { const int y = v.retainCharacters ("0123456789").substring (0, 4).getIntValue(); if (y > 1000 && out.year == 0) out.year = y; }
            else if (key == "ONM" || key == "OPT") out.comment = (out.comment.isNotEmpty() ? out.comment + " | " : juce::String()) + v;
            continue;
        }
        if (line.startsWith ("!") || line.trim().isEmpty()) continue;
        juce::StringArray f;
        f.addTokens (line, "\t", "");
        if (! started)
        {
            if (! line.startsWith ("**")) continue;
            started = true;
            for (auto& h : f)
            {
                Spine sp;
                sp.kern = h == "**kern";
                if (sp.kern) { sp.track = b.addTrack ("Spine " + juce::String (kernCount + 1)); sp.ch = melodicChannel (kernCount); ++kernCount; }
                spines.push_back (sp);
            }
            continue;
        }
        if (f.size() != (int) spines.size()) continue;   // malformed line
        if (line.startsWith ("*"))
        {
            std::vector<Spine> next;
            for (int i = 0; i < f.size(); ++i)
            {
                auto& sp = spines[(size_t) i];
                const auto& t = f[i];
                if (t == "*^") { next.push_back (sp); next.push_back (sp); continue; }
                if (t == "*v")
                {
                    if (! next.empty() && i > 0 && f[i - 1] == "*v") { next.back().pos = juce::jmax (next.back().pos, sp.pos); continue; }
                    next.push_back (sp);
                    continue;
                }
                if (t == "*-") continue;
                if (sp.kern && t.startsWith ("*MM") && ! tempoSet) { const double bpm = t.substring (3).getDoubleValue(); if (bpm > 0) { b.tempo (ticksOf (sp.pos), bpm); tempoSet = true; } }
                if (sp.kern && t.startsWith ("*M") && t.containsChar ('/') && ! t.startsWith ("*MM") && ! sigSet)
                {
                    const auto m = t.substring (2);
                    b.timeSig (ticksOf (sp.pos), m.upToFirstOccurrenceOf ("/", false, false).getIntValue(), m.fromFirstOccurrenceOf ("/", false, false).getIntValue());
                    sigSet = true;
                }
                if (sp.kern && t.startsWith ("*I\"")) { /* instrument name */ }
                next.push_back (sp);
            }
            spines = std::move (next);
            continue;
        }
        if (line.startsWith ("=")) continue;   // barlines
        for (int i = 0; i < f.size(); ++i)
            if (spines[(size_t) i].kern) tokenNotes (spines[(size_t) i], f[i]);
    }
    sink.flush();
    if (! tempoSet) b.tempo (0, 120.0);
    if (! sigSet) b.timeSig (0, 4, 4);
    if (! b.hasNotes()) { error = T ("Keine Noten gefunden"); return false; }
    out.format = "Humdrum";
    out.smf = b.build();
    return true;
}
} // namespace ma::fmt
