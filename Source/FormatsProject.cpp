// MIDIARCHIV – open project formats: DAWproject (.dawproject, Bitwig / Studio One / Fender Studio Pro / Cubase 14+)
// and REAPER (.rpp, plain text). Both are read into an SMF; Export.cpp writes them back (ExportDaw).
#include "Formats.h"
#include "SmfBuilder.h"
#include "Lang.h"
#include <map>
#include <functional>

namespace ma::fmt
{
namespace
{
constexpr int kPpq = 960;

// Tempo map in beats with square (hold) segments: converts seconds <-> beats.
struct TempoMap
{
    std::vector<std::pair<double, double>> pts;   // beat, bpm (sorted, first at 0)
    void add (double beat, double bpm) { if (bpm > 1 && bpm < 2000) pts.push_back ({ juce::jmax (0.0, beat), bpm }); }
    void finish (double fallback)
    {
        std::stable_sort (pts.begin(), pts.end(), [] (auto& a, auto& b) { return a.first < b.first; });
        if (pts.empty() || pts.front().first > 0) pts.insert (pts.begin(), { 0.0, pts.empty() ? fallback : pts.front().second });
    }
    double beatsAt (double seconds) const
    {
        double beat = 0, sec = 0;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const double bpm = pts[i].second;
            const double nextBeat = i + 1 < pts.size() ? pts[i + 1].first : 1e12;
            const double segSec = (nextBeat - pts[i].first) * 60.0 / bpm;
            if (seconds <= sec + segSec) return beat + (seconds - sec) * bpm / 60.0;
            sec += segSec; beat = nextBeat;
        }
        return beat;
    }
};

juce::int64 toTick (double beats) { return (juce::int64) std::llround (beats * kPpq); }

bool isDrumName (const juce::String& s)
{
    const auto l = s.toLowerCase();
    for (auto* k : { "drum", "schlagzeug", "perc", "kit", "beat", "impact", "groove agent", "battery", "808", "909" })
        if (l.contains (k)) return true;
    return false;
}

// Gives each DAW track its own MIDI channel (drums on 10) unless the file names channels itself.
struct ChannelPlan
{
    int melodic = 0;
    int next (bool drums)
    {
        if (drums) return 9;
        const int c = melodic % 15;
        ++melodic;
        return c >= 9 ? c + 1 : c;
    }
};
} // namespace

// ================================================================================================ DAWproject
bool readDawProject (juce::ZipFile& zip, ConvertedSong& out, juce::String& error)
{
    auto project = parseXml (zipEntryText (zip, "project.xml"));
    if (project == nullptr || ! project->hasTagName ("Project")) { error = T ("Keine DAWproject-Datei"); return false; }
    out.format = "DAWproject";

    if (auto meta = parseXml (zipEntryText (zip, "metadata.xml")))
    {
        auto f = [&] (const char* n) { auto* e = meta->getChildByName (n); return e != nullptr ? e->getAllSubText().trim() : juce::String(); };
        out.title = f ("Title"); out.artist = f ("Artist"); out.composer = f ("Composer"); out.genre = f ("Genre");
        out.copyright = f ("Copyright"); out.comment = f ("Comment"); out.year = f ("Year").getIntValue();
        if (out.composer.isEmpty()) out.composer = f ("Songwriter");
    }
    if (auto* app = project->getChildByName ("Application"))
    {
        const auto a = (app->getStringAttribute ("name") + " " + app->getStringAttribute ("version")).trim();
        if (a.isNotEmpty()) out.comment = out.comment.isNotEmpty() ? out.comment + " | " + a : a;
    }

    // ids -> elements (clip references, tracks)
    std::map<juce::String, juce::XmlElement*> byId;
    std::function<void (juce::XmlElement&)> index = [&] (juce::XmlElement& e) {
        if (e.hasAttribute ("id")) byId[e.getStringAttribute ("id")] = &e;
        for (auto* c : e.getChildIterator()) index (*c);
    };
    index (*project);

    // tempo and metre
    double bpm0 = 120;
    int num0 = 4, den0 = 4;
    juce::String tempoId, sigId;
    if (auto* tr = project->getChildByName ("Transport"))
    {
        if (auto* t = tr->getChildByName ("Tempo")) { bpm0 = t->getDoubleAttribute ("value", 120); tempoId = t->getStringAttribute ("id"); }
        if (auto* s = tr->getChildByName ("TimeSignature")) { num0 = s->getIntAttribute ("numerator", 4); den0 = s->getIntAttribute ("denominator", 4); sigId = s->getStringAttribute ("id"); }
    }
    TempoMap tempo;
    SmfBuilder b (kPpq);
    auto* arr = project->getChildByName ("Arrangement");
    if (arr != nullptr)
    {
        if (auto* ta = arr->getChildByName ("TempoAutomation"))
            for (auto* p : ta->getChildWithTagNameIterator ("RealPoint"))
                tempo.add (p->getDoubleAttribute ("time"), p->getDoubleAttribute ("value"));
    }
    tempo.finish (bpm0);
    for (auto& [beat, v] : tempo.pts) b.tempo (toTick (beat), v);
    bool sigAt0 = false;
    if (arr != nullptr)
        if (auto* sa = arr->getChildByName ("TimeSignatureAutomation"))
            for (auto* p : sa->getChildWithTagNameIterator ("TimeSignaturePoint"))
            {
                const double t = p->getDoubleAttribute ("time");
                if (t <= 0) sigAt0 = true;
                b.timeSig (toTick (t), p->getIntAttribute ("numerator", 4), p->getIntAttribute ("denominator", 4));
            }
    if (! sigAt0) b.timeSig (0, num0, den0);
    const double secToBeat = bpm0 / 60.0;   // nested "seconds" timelines: first tempo (rare for notes)

    // tracks in Structure order (folders flattened)
    struct TrackInfo { juce::String name; bool drums = false; int smfTrack = -1, channel = 0; };
    std::map<juce::String, TrackInfo> tracks;
    juce::StringArray order;
    std::function<void (juce::XmlElement&)> collect = [&] (juce::XmlElement& e) {
        for (auto* c : e.getChildIterator())
        {
            if (! c->hasTagName ("Track")) continue;
            TrackInfo ti;
            ti.name = c->getStringAttribute ("name");
            juce::String devices;
            if (auto* ch = c->getChildByName ("Channel"))
                if (auto* dv = ch->getChildByName ("Devices"))
                    for (auto* d : dv->getChildIterator()) devices << d->getStringAttribute ("deviceName") << " ";
            ti.drums = isDrumName (ti.name) || isDrumName (devices);
            tracks[c->getStringAttribute ("id")] = ti;
            order.add (c->getStringAttribute ("id"));
            collect (*c);
        }
    };
    if (auto* st = project->getChildByName ("Structure")) collect (*st);
    ChannelPlan plan;
    auto trackFor = [&] (const juce::String& id) -> TrackInfo* {
        auto it = tracks.find (id);
        if (it == tracks.end()) { tracks[id] = {}; it = tracks.find (id); }
        auto& t = it->second;
        if (t.smfTrack < 0) { t.smfTrack = b.addTrack (t.name); t.channel = plan.next (t.drums); }
        return &t;
    };

    // timeline walk. offset: absolute beat of content time 0; [cw0, cw1): visible content range; [w0, w1): absolute window
    struct Ctx { juce::String track; double offset = 0, cw0 = -1e12, cw1 = 1e12, w0 = -1e12, w1 = 1e12; bool seconds = false; };
    double songEnd = 0;
    int notes = 0;
    std::function<void (juce::XmlElement&, Ctx)> walk = [&] (juce::XmlElement& e, Ctx ctx) {
        if (e.hasAttribute ("track")) ctx.track = e.getStringAttribute ("track");
        if (e.hasAttribute ("timeUnit")) ctx.seconds = e.getStringAttribute ("timeUnit") == "seconds";
        const double unit = ctx.seconds ? secToBeat : 1.0;
        if (e.hasTagName ("Note"))
        {
            const double t = e.getDoubleAttribute ("time") * unit, d = e.getDoubleAttribute ("duration") * unit;
            if (t < ctx.cw0 - 1e-6 || t >= ctx.cw1 - 1e-6) return;
            double on = ctx.offset + t, off = on + d;
            if (on < ctx.w0 - 1e-6 || on >= ctx.w1 - 1e-6) return;
            off = juce::jmin (off, ctx.w1, ctx.offset + ctx.cw1);
            auto* tr = trackFor (ctx.track);
            double vel = e.getDoubleAttribute ("vel", 0.8);
            const int v = vel > 1.5 ? (int) vel : juce::roundToInt (vel * 127.0);
            int ch = e.getIntAttribute ("channel", 0);
            if (ch <= 0 || ch > 15) ch = tr->channel;
            b.note (tr->smfTrack, ch, toTick (on), toTick (off), e.getIntAttribute ("key", 60), juce::jlimit (1, 127, v));
            songEnd = juce::jmax (songEnd, off);
            ++notes;
            return;
        }
        if (e.hasTagName ("Clip"))
        {
            if (! e.getBoolAttribute ("enable", true)) return;
            const double start = ctx.offset + e.getDoubleAttribute ("time") * unit;
            const bool cSec = e.getStringAttribute ("contentTimeUnit") == "seconds";
            const double cu = cSec ? secToBeat : (e.hasAttribute ("contentTimeUnit") ? 1.0 : unit);
            const double ps = e.getDoubleAttribute ("playStart", 0) * cu;
            double dur = e.hasAttribute ("duration") ? e.getDoubleAttribute ("duration") * unit
                                                     : (e.hasAttribute ("playStop") ? e.getDoubleAttribute ("playStop") * cu - ps : 1e9);
            const double stop = e.hasAttribute ("playStop") ? e.getDoubleAttribute ("playStop") * cu : 1e12;
            const double ls = e.getDoubleAttribute ("loopStart", 0) * cu, le = e.getDoubleAttribute ("loopEnd", 0) * cu;
            juce::Array<juce::XmlElement*> content;
            if (e.hasAttribute ("reference")) { if (auto it = byId.find (e.getStringAttribute ("reference")); it != byId.end()) content.add (it->second); }
            for (auto* c : e.getChildIterator()) content.add (c);
            Ctx c = ctx;
            c.seconds = cSec;
            c.w0 = juce::jmax (ctx.w0, start);
            c.w1 = juce::jmin (ctx.w1, start + dur);
            if (le > ls + 1e-6)
            {
                // first pass from playStart to loopEnd, then loopStart..loopEnd until the clip ends
                double pos = start;
                double from = ps, to = le;
                for (int k = 0; k < 4096 && pos < c.w1 - 1e-6; ++k)
                {
                    Ctx p = c;
                    p.offset = pos - from; p.cw0 = from; p.cw1 = to;
                    for (auto* x : content) walk (*x, p);
                    pos += to - from;
                    from = ls;
                    if (to - from <= 1e-6) break;
                }
            }
            else
            {
                c.offset = start - ps; c.cw0 = ps; c.cw1 = stop;
                for (auto* x : content) walk (*x, c);
            }
            return;
        }
        if (e.hasTagName ("Points") || e.hasTagName ("Audio") || e.hasTagName ("Video") || e.hasTagName ("Warps")
            || e.hasTagName ("TempoAutomation") || e.hasTagName ("TimeSignatureAutomation") || e.hasTagName ("Markers")) return;
        for (auto* c : e.getChildIterator()) walk (*c, ctx);
    };

    if (arr != nullptr)
        if (auto* lanes = arr->getChildByName ("Lanes")) walk (*lanes, {});
    if (notes == 0)   // clip launcher only: scenes one after another
        if (auto* scenes = project->getChildByName ("Scenes"))
        {
            double pos = 0;
            for (auto* sc : scenes->getChildWithTagNameIterator ("Scene"))
            {
                double len = 0;
                std::function<void (juce::XmlElement&)> measure = [&] (juce::XmlElement& x) {
                    if (x.hasTagName ("Clip"))
                    {
                        const double d = x.hasAttribute ("duration") ? x.getDoubleAttribute ("duration")
                                                                     : x.getDoubleAttribute ("playStop") - x.getDoubleAttribute ("playStart");
                        len = juce::jmax (len, d);
                        return;
                    }
                    for (auto* c : x.getChildIterator()) measure (*c);
                };
                measure (*sc);
                if (len <= 0) continue;
                Ctx c;
                c.offset = pos; c.w0 = pos; c.w1 = pos + len;
                for (auto* x : sc->getChildIterator()) walk (*x, c);
                pos += len;
            }
        }
    if (arr != nullptr)
        if (auto* mk = arr->getChildByName ("Markers"))
            for (auto* m : mk->getChildWithTagNameIterator ("Marker"))
                b.meta (0, toTick (m->getDoubleAttribute ("time")), 0x06, m->getStringAttribute ("name"));
    if (! b.hasNotes()) { error = T ("Keine Noten gefunden (nur Audio?)"); return false; }
    b.endAt (toTick (songEnd));
    out.smf = b.build();
    return true;
}

// ================================================================================================ REAPER
namespace
{
juce::StringArray rppTokens (const juce::String& line)
{
    juce::StringArray t;
    auto p = line.getCharPointer();
    while (! p.isEmpty())
    {
        while (*p == ' ' || *p == '\t') ++p;
        if (p.isEmpty()) break;
        juce::String tok;
        const auto q = *p;
        if (q == '"' || q == '\'' || q == '`')
        {
            ++p;
            while (! p.isEmpty() && *p != q) tok += *p++;
            if (! p.isEmpty()) ++p;
        }
        else
            while (! p.isEmpty() && *p != ' ' && *p != '\t') tok += *p++;
        t.add (tok);
    }
    return t;
}

struct RppNode
{
    juce::StringArray head;                 // tokens of the "<NAME ..." line
    std::vector<juce::StringArray> lines;   // plain lines (tokenised), in order
    std::vector<std::unique_ptr<RppNode>> kids;
    std::vector<int> order;                 // -1-k = kid k, otherwise index into lines
    juce::String name() const { return head.isEmpty() ? juce::String() : head[0]; }
    const juce::StringArray* line (const juce::String& key) const
    {
        for (auto& l : lines) if (l.size() > 0 && l[0] == key) return &l;
        return nullptr;
    }
    RppNode* kid (const juce::String& n) const
    {
        for (auto& k : kids) if (k->name() == n) return k.get();
        return nullptr;
    }
};

std::unique_ptr<RppNode> parseRpp (const juce::String& text)
{
    auto root = std::make_unique<RppNode>();
    std::vector<RppNode*> stack { root.get() };
    juce::StringArray rows;
    rows.addLines (text);
    for (auto& raw : rows)
    {
        const auto s = raw.trim();
        if (s.isEmpty()) continue;
        auto* cur = stack.back();
        if (s.startsWithChar ('<'))
        {
            auto n = std::make_unique<RppNode>();
            n->head = rppTokens (s.substring (1));
            auto* np = n.get();
            cur->order.push_back (-1 - (int) cur->kids.size());
            cur->kids.push_back (std::move (n));
            stack.push_back (np);
        }
        else if (s == ">") { if (stack.size() > 1) stack.pop_back(); }
        else
        {
            cur->order.push_back ((int) cur->lines.size());
            cur->lines.push_back (rppTokens (s));
        }
    }
    return root;
}
} // namespace

bool readReaper (const juce::String& text, ConvertedSong& out, juce::String& error)
{
    auto root = parseRpp (text);
    auto* proj = root->kid ("REAPER_PROJECT");
    if (proj == nullptr) { error = T ("Keine REAPER-Projektdatei"); return false; }
    out.format = "REAPER";
    if (proj->head.size() > 2) out.comment = "REAPER " + proj->head[2];

    // metadata: project notes and render metadata (ID3 / INFO tags)
    if (auto* n = proj->kid ("NOTES"))
    {
        juce::StringArray t;
        for (auto& l : n->lines) t.add (l.joinIntoString (" ").fromFirstOccurrenceOf ("|", false, false));
        if (const auto c = t.joinIntoString ("\n").trim(); c.isNotEmpty()) out.comment = c + (out.comment.isNotEmpty() ? " | " + out.comment : juce::String());
    }
    if (auto* md = proj->kid ("RENDER_METADATA"))
        for (auto& l : md->lines)
        {
            if (l.size() < 3 || l[0] != "TAG") continue;
            const auto k = l[1].fromFirstOccurrenceOf (":", false, false).toUpperCase();
            const auto v = l[2];
            if (k == "TIT2" || k == "INAM" || k == "TITLE") out.title = v;
            else if (k == "TPE1" || k == "IART" || k == "ARTIST") out.artist = v;
            else if (k == "TCOM" || k == "COMPOSER") out.composer = v;
            else if (k == "TCON" || k == "IGNR" || k == "GENRE") out.genre = v;
            else if (k == "TYER" || k == "TDRC" || k == "ICRD" || k == "DATE" || k == "YEAR") out.year = v.substring (0, 4).getIntValue();
            else if (k == "COMM" || k == "ICMT" || k == "COMMENT" || k == "DESC") out.comment = v;
            else if (k == "TCOP" || k == "ICOP" || k == "COPYRIGHT") out.copyright = v;
        }

    // tempo map: TEMPO line + TEMPOENVEX points (time in seconds)
    double bpm0 = 120;
    int num0 = 4, den0 = 4;
    if (auto* t = proj->line ("TEMPO"))
    {
        bpm0 = (*t)[1].getDoubleValue();
        if (t->size() > 3) { num0 = (*t)[2].getIntValue(); den0 = (*t)[3].getIntValue(); }
    }
    if (bpm0 <= 1) bpm0 = 120;
    struct Pt { double sec, bpm; int sig; };
    std::vector<Pt> pts;
    if (auto* env = proj->kid ("TEMPOENVEX"))
        for (auto& l : env->lines)
            if (l.size() >= 3 && l[0] == "PT")
                pts.push_back ({ l[1].getDoubleValue(), l[2].getDoubleValue(), l.size() > 4 ? l[4].getIntValue() : 0 });
    std::stable_sort (pts.begin(), pts.end(), [] (auto& a, auto& b) { return a.sec < b.sec; });
    TempoMap tempo;
    {
        // seconds of each point -> beats, walking the square segments
        double sec = 0, beat = 0, bpm = pts.empty() || pts.front().sec > 0 ? bpm0 : pts.front().bpm;
        tempo.add (0, bpm);
        for (auto& p : pts)
        {
            if (p.sec <= 0) { bpm = p.bpm; tempo.pts.back().second = p.bpm; continue; }
            beat += (p.sec - sec) * bpm / 60.0;
            sec = p.sec; bpm = p.bpm;
            tempo.add (beat, bpm);
        }
        tempo.finish (bpm0);
    }
    SmfBuilder b (kPpq);
    for (auto& [beat, v] : tempo.pts) b.tempo (toTick (beat), v);
    b.timeSig (0, num0 > 0 ? num0 : 4, den0 > 0 ? den0 : 4);
    for (auto& p : pts)
        if (p.sig > 0 && (p.sig & 0xFFFF) > 0)
            b.timeSig (toTick (tempo.beatsAt (p.sec)), p.sig & 0xFFFF, juce::jmax (1, p.sig >> 16));
    for (auto& l : proj->lines)   // MARKER idx seconds "name" flags
        if (l.size() >= 4 && l[0] == "MARKER" && l[3].isNotEmpty())
            b.meta (0, toTick (tempo.beatsAt (l[2].getDoubleValue())), 0x06, l[3]);

    double songEnd = 0;
    int notes = 0;
    for (auto& tk : proj->kids)
    {
        if (tk->name() != "TRACK") continue;
        juce::String name;
        if (auto* n = tk->line ("NAME")) name = (*n)[1];
        int smfTrack = -1;
        for (auto& it : tk->kids)
        {
            if (it->name() != "ITEM") continue;
            if (auto* m = it->line ("MUTE"); m != nullptr && (*m)[1].getIntValue() != 0) continue;
            const double pos = it->line ("POSITION") != nullptr ? (*it->line ("POSITION"))[1].getDoubleValue() : 0;
            const double len = it->line ("LENGTH") != nullptr ? (*it->line ("LENGTH"))[1].getDoubleValue() : 0;
            const bool loop = it->line ("LOOP") != nullptr && (*it->line ("LOOP"))[1].getIntValue() != 0;
            // active take: SOURCE after "TAKE SEL", otherwise the first one
            const RppNode* src = nullptr;
            double soffs = it->line ("SOFFS") != nullptr ? (*it->line ("SOFFS"))[1].getDoubleValue() : 0;
            {
                bool selNext = false, takeSeen = false;
                const RppNode* first = nullptr;
                for (int o : it->order)
                {
                    if (o >= 0)
                    {
                        auto& l = it->lines[(size_t) o];
                        if (l.size() > 0 && l[0] == "TAKE") { takeSeen = true; selNext = l.contains ("SEL"); }
                        if (l.size() > 1 && l[0] == "SOFFS" && selNext) soffs = l[1].getDoubleValue();
                        continue;
                    }
                    auto* k = it->kids[(size_t) (-1 - o)].get();
                    if (k->name() != "SOURCE") continue;
                    if (first == nullptr) first = k;
                    if (takeSeen && selNext) { src = k; break; }
                }
                if (src == nullptr) src = first;
            }
            if (src == nullptr || (src->head[1] != "MIDI" && src->head[1] != "MIDIPOOL")) continue;
            int ppq = 960;
            bool ignTempo = false;
            double ignBpm = 120;
            if (auto* h = src->line ("HASDATA")) { if (h->size() > 2) ppq = juce::jmax (1, (*h)[2].getIntValue()); }
            if (auto* ig = src->line ("IGNTEMPO"); ig != nullptr && (*ig)[1].getIntValue() != 0) { ignTempo = true; ignBpm = juce::jmax (1.0, (*ig)[2].getDoubleValue()); }

            // events: (source tick, bytes)
            struct Ev { juce::int64 tick; juce::uint8 st, d1, d2; };
            std::vector<Ev> evs;
            juce::int64 tick = 0;
            for (int o : src->order)
            {
                if (o < 0)   // <X delta ...> long event: only its delta counts
                {
                    auto* k = src->kids[(size_t) (-1 - o)].get();
                    if ((k->name() == "X" || k->name() == "x") && k->head.size() > 1) tick += k->head[1].getLargeIntValue();
                    continue;
                }
                auto& l = src->lines[(size_t) o];
                if (l.size() < 2) continue;
                if (l[0] == "E" || l[0] == "e")
                {
                    tick += l[1].getLargeIntValue();
                    if (l.size() >= 4)
                        evs.push_back ({ tick, (juce::uint8) l[2].getHexValue32(), (juce::uint8) l[3].getHexValue32(), (juce::uint8) (l.size() > 4 ? l[4].getHexValue32() : 0) });
                }
                else if (l[0] == "X" || l[0] == "x") tick += l[1].getLargeIntValue();
            }
            const juce::int64 srcLen = tick;
            if (smfTrack < 0) smfTrack = b.addTrack (name);

            const double startBeat = tempo.beatsAt (pos), endBeat = tempo.beatsAt (pos + len);
            // source tick -> absolute beat (QN ticks follow the project tempo; IGNTEMPO uses its own fixed tempo)
            const double soffsQn = ignTempo ? soffs * ignBpm / 60.0 : tempo.beatsAt (pos + soffs) - startBeat;
            auto beatOf = [&] (double qn) {
                if (! ignTempo) return startBeat + qn - soffsQn;
                return tempo.beatsAt (pos + (qn - soffsQn) * 60.0 / ignBpm);
            };
            const double srcQn = (double) srcLen / ppq;
            const int passes = loop && srcQn > 1e-6 ? juce::jlimit (1, 4096, (int) std::ceil ((endBeat - startBeat + soffsQn) / srcQn) + 1) : 1;
            for (int pass = 0; pass < passes; ++pass)
            {
                std::map<int, std::vector<std::pair<double, int>>> open;   // (ch << 8 | key) -> starts
                const double base = pass * srcQn;
                auto emitNote = [&] (int ch, int key, double on, int vel, double off) {
                    const double a = beatOf (on), z = juce::jmin (beatOf (off), endBeat);
                    if (a < startBeat - 1e-6 || a >= endBeat - 1e-6) return;
                    b.note (smfTrack, ch, toTick (a), toTick (z), key, vel);
                    songEnd = juce::jmax (songEnd, z);
                    ++notes;
                };
                for (auto& e : evs)
                {
                    const double qn = base + (double) e.tick / ppq;
                    const int type = e.st & 0xF0, ch = e.st & 15;
                    if (type == 0x90 && e.d2 > 0) open[(ch << 8) | e.d1].push_back ({ qn, e.d2 });
                    else if (type == 0x80 || type == 0x90)
                    {
                        auto& v = open[(ch << 8) | e.d1];
                        if (v.empty()) continue;
                        emitNote (ch, e.d1, v.front().first, v.front().second, qn);
                        v.erase (v.begin());
                    }
                    else if (type == 0xB0 || type == 0xC0 || type == 0xE0 || type == 0xD0 || type == 0xA0)
                    {
                        if (type == 0xB0 && (e.d1 == 0x7B || e.d1 == 0x78)) continue;   // REAPER's end-of-source all-notes-off
                        const double a = beatOf (qn);
                        if (a < startBeat - 1e-6 || a >= endBeat - 1e-6) continue;
                        b.channelEvent (smfTrack, toTick (a), e.st, e.d1, (type == 0xC0 || type == 0xD0) ? -1 : e.d2, 1);
                    }
                }
                for (auto& [k, v] : open)   // hanging notes end with the source
                    for (auto& s : v) emitNote (k >> 8, k & 0xFF, s.first, s.second, base + srcQn);
            }
        }
    }
    if (notes == 0) { error = T ("Keine Noten gefunden (nur Audio?)"); return false; }
    b.endAt (toTick (songEnd));
    out.smf = b.build();
    return true;
}
} // namespace ma::fmt
