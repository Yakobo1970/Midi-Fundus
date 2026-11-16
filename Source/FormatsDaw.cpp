// MIDIARCHIV – sequencer / DAW projects: Cakewalk WRK, FL Studio FLP, Ableton Live ALS, Studio One / Fender Studio Pro SONG.
#include "Formats.h"
#include "SmfBuilder.h"
#include "Lang.h"
#include <map>

namespace ma::fmt
{
namespace
{
juce::uint32 rd16 (const juce::uint8* p) { return (juce::uint32) (p[0] | (p[1] << 8)); }
juce::uint32 rd24 (const juce::uint8* p) { return (juce::uint32) (p[0] | (p[1] << 8) | (p[2] << 16)); }
juce::uint32 rd32 (const juce::uint8* p) { return (juce::uint32) (p[0] | (p[1] << 8) | (p[2] << 16) | ((juce::uint32) p[3] << 24)); }
}

// ============================================================================================ Cakewalk WRK
// "CAKEWALK" 0x1A, version bytes, then chunks: id (1 byte), length (u32 LE), data; 0xFF ends the file.
// Events: time (u24), status; old streams (chunk 2) use fixed 8-byte records (status, d1, d2, duration u16),
// newer streams (45) and segments (49) variable records (d2 only for 9x/Ax/Bx/Ex, duration only for notes).
bool readWrk (const juce::MemoryBlock& data, ConvertedSong& out, juce::String& error)
{
    auto* d = static_cast<const juce::uint8*> (data.getData());
    const size_t n = data.getSize();
    if (n < 12) { error = T ("Datei nicht lesbar"); return false; }
    struct Ev { juce::uint32 t; int st, d1, d2, dur; };
    struct Trk { juce::String name; int patch = -1, bank = -1, channel = -1; std::vector<Ev> evs; };
    std::map<int, Trk> trks;
    int ppq = 120, meterNum = 0, meterDen = 4;
    std::vector<std::pair<juce::uint32, double>> tempos;
    juce::String title, subtitle, author, copyright, keywords;

    auto parseEvents = [&] (Trk& trk, const juce::uint8* p, const juce::uint8* end, juce::uint32 count, bool fixed) {
        for (juce::uint32 i = 0; i < count; ++i)
        {
            if (p + 4 > end) return;
            const auto t = rd24 (p);
            const int st = p[3];
            p += 4;
            if (st >= 0x80)
            {
                const int ty = st & 0xF0;
                Ev e { t, st, 0, -1, 0 };
                if (fixed)
                {
                    if (p + 4 > end) return;
                    e.d1 = p[0]; e.d2 = p[1]; e.dur = (int) rd16 (p + 2);
                    p += 4;
                    if (ty == 0xC0 || ty == 0xD0) e.d2 = -1;
                }
                else
                {
                    if (p + 1 > end) return;
                    e.d1 = *p++;
                    if (ty == 0x90 || ty == 0xA0 || ty == 0xB0 || ty == 0xE0) { if (p + 1 > end) return; e.d2 = *p++; }
                    if (ty == 0x90) { if (p + 2 > end) return; e.dur = (int) rd16 (p); p += 2; }
                }
                if (ty >= 0x90 && ty <= 0xE0) trk.evs.push_back (e);
            }
            else if (fixed) p += 4;
            else if (st == 5) { if (p + 6 > end) return; p += 6 + rd32 (p + 2); }
            else if (st == 6) p += 8;
            else if (st == 7) { if (p + 6 > end) return; p += 6 + rd32 (p + 2); }
            else if (st == 8) { if (p + 2 > end) return; p += 2 + rd16 (p); }
            else { if (p + 4 > end) return; p += 4 + rd32 (p); }
        }
    };

    size_t pos = 11;
    while (pos + 5 <= n)
    {
        const int id = d[pos];
        if (id == 0xFF) break;
        const auto len = rd32 (d + pos + 1);
        if (pos + 5 + len > n) break;
        const auto* c = d + pos + 5;
        const auto* ce = c + len;
        pos += 5 + len;
        auto str = [&] (const juce::uint8* p, size_t l) {
            l = (size_t) juce::jmax<std::ptrdiff_t> (0, juce::jmin<std::ptrdiff_t> ((std::ptrdiff_t) l, ce - p));
            size_t z = 0;
            while (z < l && p[z] != 0) ++z;   // fixed-size fields are zero-padded
            return decodeText (p, z).trim();
        };
        switch (id)
        {
            case 10: if (len >= 2) ppq = juce::jlimit (24, 15360, (int) rd16 (c)); break;
            case 4: case 15:
                if (len >= 2)
                {
                    const int cnt = (int) rd16 (c);
                    const int stride = cnt > 0 ? (int) ((len - 2) / (juce::uint32) cnt) : 0;
                    for (int i = 0; i < cnt && stride >= 10; ++i)
                    {
                        const auto* e = c + 2 + i * stride;
                        tempos.push_back ({ rd32 (e), rd16 (e + 8) / 100.0 });
                    }
                }
                break;
            case 23: if (len >= 7 && rd16 (c) > 0) { meterNum = c[4]; meterDen = 1 << juce::jmin (6, (int) c[5]); } break;
            case 5:  if (len >= 12 && rd16 (c) > 0) { meterNum = c[8]; meterDen = 1 << juce::jmin (6, (int) c[9]); } break;
            case 26:
                if (len > 32)
                {
                    const auto key = str (c, 32);
                    const auto value = str (c + 32, len - 32);
                    if (key == "Title") title = value;
                    else if (key == "Subtitle") subtitle = value;
                    else if (key == "Author") author = value;
                    else if (key == "Copyright") copyright = value;
                    else if (key == "Keywords") keywords = value;
                }
                break;
            case 1:   // old track: number, name, second name, channel (signed, -1 = events keep theirs)
                if (len >= 3)
                {
                    auto& t = trks[(int) rd16 (c)];
                    const int l1 = c[2];
                    if (t.name.isEmpty()) t.name = str (c + 3, (size_t) l1);
                    const auto* q = c + 3 + l1;
                    if (q < ce) { q += 1 + *q; if (q < ce) { const auto ch = (juce::int8) *q; if (ch >= 0 && ch < 16) t.channel = ch; } }
                }
                break;
            case 24:
                if (len >= 3) trks[(int) rd16 (c)].name = str (c + 3, c[2]);
                break;
            case 36:  // new track: number, name, bank, patch, volume, pan, key, velocity, 7 gap, port, channel
                if (len >= 3)
                {
                    auto& t = trks[(int) rd16 (c)];
                    const int nl = c[2];
                    t.name = str (c + 3, (size_t) nl);
                    const auto* q = c + 3 + nl;
                    if (q + 19 <= ce)
                    {
                        const auto bank = (juce::int16) rd16 (q), patch = (juce::int16) rd16 (q + 2);
                        if (patch >= 0 && patch < 128) t.patch = patch;
                        if (bank >= 0) t.bank = bank;
                        const int ch = q[18];
                        if (ch < 16) t.channel = ch;
                    }
                }
                break;
            case 14: if (len >= 3) trks[(int) rd16 (c)].patch = c[2] & 0x7F; break;
            case 2:   // old stream
                if (len >= 4) parseEvents (trks[(int) rd16 (c)], c + 4, ce, rd16 (c + 2), true);
                break;
            case 18:  // stream (version 2): track, u32 count, variable records
                if (len >= 6) parseEvents (trks[(int) rd16 (c)], c + 6, ce, rd32 (c + 2), false);
                break;
            case 45:  // new stream
                if (len >= 7)
                {
                    const int nl = c[2];
                    if (c + 7 + nl <= ce) parseEvents (trks[(int) rd16 (c)], c + 7 + nl, ce, rd32 (c + 3 + nl), false);
                }
                break;
            case 49:  // segment with embedded events (short form = header of the following stream)
                if (len >= 20)
                {
                    const int nl = c[14];
                    const auto* q = c + 15 + nl + 20;
                    if (q + 4 <= ce) parseEvents (trks[(int) rd16 (c)], q + 4, ce, rd32 (q), false);
                }
                break;
            default: break;
        }
    }

    SmfBuilder b (ppq);
    std::sort (tempos.begin(), tempos.end());
    if (tempos.empty()) b.tempo (0, 120.0);
    for (auto& [t, bpm] : tempos) if (bpm > 0) b.tempo ((juce::int64) t, bpm);
    if (meterNum > 0) b.timeSig (0, meterNum, meterDen);
    for (auto& [num, t] : trks)
    {
        if (t.evs.empty()) continue;
        const int tr = b.addTrack (t.name);
        const int forced = t.channel;
        const int firstCh = forced >= 0 ? forced : (t.evs.front().st & 15);
        if (t.patch >= 0) b.program (tr, 0, firstCh, t.patch);
        for (auto& e : t.evs)
        {
            const int ch = forced >= 0 ? forced : (e.st & 15);
            const int ty = e.st & 0xF0;
            if (ty == 0x90) { if (e.d2 > 0) b.note (tr, ch, e.t, (juce::int64) e.t + juce::jmax (1, e.dur), e.d1, e.d2); }
            else b.channelEvent (tr, e.t, ty | ch, e.d1, e.d2, 1);
        }
    }
    if (! b.hasNotes()) { error = T ("Keine Noten gefunden"); return false; }
    out.format = "Cakewalk";
    out.smf = b.build();
    auto tt = title.removeCharacters ("\"").trim();
    if (tt.containsIgnoreCase (" by "))
    {
        out.artist = tt.fromLastOccurrenceOf (" by ", false, true).trim();
        tt = tt.upToLastOccurrenceOf (" by ", false, true).trim();
    }
    out.title = tt;
    out.copyright = copyright;
    juce::StringArray cm;
    for (auto* v : { &subtitle, &author, &keywords }) if (v->isNotEmpty()) cm.add (*v);
    out.comment = cm.joinIntoString (" | ");
    return true;
}
// ============================================================================================ FL Studio FLP
// "FLhd" (format, channel count, PPQ) + "FLdt" event stream: id < 64 byte, < 128 word, < 192 dword, else
// variable-length data (7-bit little-endian length). Used here: 64 new channel, 203 channel name, 65 new pattern,
// 193 pattern name, 224 pattern notes (24 bytes each), 233 playlist (32-byte items), 156 tempo (BPM * 1000),
// 194 title, 195 comment, 206 genre, 207 artist, 199 program version. Texts are UTF-16LE (ASCII in old versions).
bool readFlp (const juce::MemoryBlock& data, ConvertedSong& out, juce::String& error)
{
    auto* d = static_cast<const juce::uint8*> (data.getData());
    const size_t n = data.getSize();
    if (n < 22 || memcmp (d + 14, "FLdt", 4) != 0) { error = T ("Datei nicht lesbar"); return false; }
    const int ppq = juce::jlimit (24, 3840, (int) rd16 (d + 12));
    struct FlNote { juce::uint32 pos, len; int chan, key, vel; };
    std::map<int, std::vector<FlNote>> patterns;
    std::map<int, juce::String> channelNames, patternNames;
    struct Item { juce::uint32 pos, len; int pattern; double startOff; };
    std::vector<Item> items;
    double bpm = 0;
    juce::String title, comment, genre, artist, version;
    int curChannel = -1, curPattern = 0;

    auto text = [] (const juce::uint8* p, size_t len) {
        juce::String s;
        if (len >= 2 && p[1] == 0 && (len % 2) == 0)
        {
            for (size_t i = 0; i + 1 < len; i += 2)
            {
                const auto c = (juce::juce_wchar) (p[i] | (p[i + 1] << 8));
                if (c == 0) break;
                s += c;
            }
        }
        else s = decodeText (p, strnlen ((const char*) p, len));
        return s.trim();
    };

    size_t pos = 22;
    while (pos < n)
    {
        const int id = d[pos++];
        juce::uint32 v = 0;
        const juce::uint8* blob = nullptr;
        size_t blen = 0;
        if (id < 64) { if (pos + 1 > n) break; v = d[pos]; pos += 1; }
        else if (id < 128) { if (pos + 2 > n) break; v = rd16 (d + pos); pos += 2; }
        else if (id < 192) { if (pos + 4 > n) break; v = rd32 (d + pos); pos += 4; }
        else
        {
            size_t len = 0;
            int shift = 0;
            while (pos < n)
            {
                const auto b = d[pos++];
                len |= (size_t) (b & 0x7F) << shift;
                shift += 7;
                if ((b & 0x80) == 0 || shift > 28) break;
            }
            if (pos + len > n) break;
            blob = d + pos;
            blen = len;
            pos += len;
        }
        switch (id)
        {
            case 64:  curChannel = (int) v; break;
            case 65:  curPattern = (int) v; break;
            case 156: bpm = v / 1000.0; break;
            case 194: title = text (blob, blen); break;
            case 195: comment = text (blob, blen); break;
            case 206: genre = text (blob, blen); break;
            case 207: artist = text (blob, blen); break;
            case 199: version = text (blob, blen); break;
            case 203: if (curChannel >= 0) channelNames[curChannel] = text (blob, blen); break;
            case 193: patternNames[curPattern] = text (blob, blen); break;
            case 224:
                for (size_t i = 0; i + 24 <= blen; i += 24)
                {
                    const auto* q = blob + i;
                    patterns[curPattern].push_back ({ rd32 (q), rd32 (q + 8), (int) rd16 (q + 6), (int) rd16 (q + 12), (int) q[21] });
                }
                break;
            case 233:
            {
                const size_t stride = (blen % 32) == 0 ? 32 : ((blen % 28) == 0 ? 28 : 32);
                for (size_t i = 0; i + stride <= blen; i += stride)
                {
                    const auto* q = blob + i;
                    const int base = (int) rd16 (q + 4), index = (int) rd16 (q + 6);
                    if (index <= base) continue;   // audio / automation clip
                    float so = 0;
                    if (stride >= 32) memcpy (&so, q + 24, 4);
                    items.push_back ({ rd32 (q), rd32 (q + 8), index - base, so > 0 ? (double) so : 0.0 });
                }
                break;
            }
            default: break;
        }
    }

    SmfBuilder b (ppq);
    b.tempo (0, bpm > 0 ? bpm : 120.0);
    std::map<int, int> trackOf;   // rack channel -> SMF track
    auto trackFor = [&] (int chan) {
        if (auto it = trackOf.find (chan); it != trackOf.end()) return it->second;
        const auto name = channelNames.count (chan) ? channelNames[chan] : juce::String ("Channel ") + juce::String (chan + 1);
        return trackOf[chan] = b.addTrack (name);
    };
    auto midiCh = [] (int chan) { const int c = chan % 15; return c >= 9 ? c + 1 : c; };
    auto place = [&] (const std::vector<FlNote>& notes, juce::int64 at, juce::int64 from, juce::int64 length) {
        for (auto& nt : notes)
        {
            const auto p = (juce::int64) nt.pos;
            if (p < from || (length > 0 && p >= from + length)) continue;
            const auto on = at + p - from;
            auto off = on + juce::jmax<juce::int64> (1, nt.len);
            if (length > 0) off = juce::jmin (off, at + length);
            b.note (trackFor (nt.chan), midiCh (nt.chan), on, off, juce::jlimit (0, 127, nt.key), juce::jlimit (1, 127, nt.vel * 127 / 128));
        }
    };
    if (! items.empty())
    {
        for (auto& it : items)
            if (auto p = patterns.find (it.pattern); p != patterns.end())
                place (p->second, it.pos, (juce::int64) it.startOff, it.len);
    }
    else
    {
        juce::int64 at = 0;   // no arrangement: patterns one after another
        for (auto& [id, notes] : patterns)
        {
            juce::int64 len = 0;
            for (auto& nt : notes) len = juce::jmax (len, (juce::int64) (nt.pos + nt.len));
            place (notes, at, 0, 0);
            at += ((len + ppq * 4 - 1) / (ppq * 4)) * ppq * 4;
        }
    }
    if (! b.hasNotes()) { error = T ("Keine Noten gefunden"); return false; }
    out.format = "FL Studio";
    out.smf = b.build();
    out.title = title; out.artist = artist; out.genre = genre;
    out.comment = comment;
    if (version.isNotEmpty()) out.comment = (out.comment.isNotEmpty() ? out.comment + " | " : juce::String()) + "FL Studio " + version;
    return true;
}
// ============================================================================================ Ableton Live ALS
// Gzip-compressed XML. MidiTrack → DeviceChain → MainSequencer: arrangement clips under ClipTimeable/ArrangerAutomation/Events,
// session clips under ClipSlotList. MidiClip: CurrentStart/CurrentEnd (arrangement span, beats), Loop (LoopStart, LoopEnd,
// StartRelative, LoopOn), Notes/KeyTracks/KeyTrack (MidiKey + MidiNoteEvent Time/Duration/Velocity, clip time in beats).
// Without arrangement clips the session scenes are laid out one after another.
namespace
{
juce::XmlElement* child (juce::XmlElement* e, const char* name) { return e != nullptr ? e->getChildByName (name) : nullptr; }
double valueOf (juce::XmlElement* e, const char* name, double def = 0)
{
    auto* c = child (e, name);
    return c != nullptr ? c->getDoubleAttribute ("Value", def) : def;
}
juce::XmlElement* findDeep (juce::XmlElement* e, const juce::String& name)
{
    if (e == nullptr) return nullptr;
    if (e->hasTagName (name)) return e;
    for (auto* c : e->getChildIterator())
        if (auto* f = findDeep (c, name)) return f;
    return nullptr;
}
void collectDeep (juce::XmlElement* e, const juce::String& name, juce::Array<juce::XmlElement*>& out)
{
    if (e == nullptr) return;
    for (auto* c : e->getChildIterator())
    {
        if (c->hasTagName (name)) out.add (c);
        else collectDeep (c, name, out);
    }
}

struct LiveNote { double t, dur; int key, vel; };

std::vector<LiveNote> clipNotes (juce::XmlElement* clip)
{
    std::vector<LiveNote> v;
    juce::Array<juce::XmlElement*> keyTracks;
    collectDeep (child (clip, "Notes"), "KeyTrack", keyTracks);
    for (auto* kt : keyTracks)
    {
        const int key = (int) valueOf (kt, "MidiKey", -1);
        if (key < 0 || key > 127) continue;
        if (auto* notes = child (kt, "Notes"))
            for (auto* ne : notes->getChildIterator())
                if (ne->getStringAttribute ("IsEnabled", "true") != "false")
                    v.push_back ({ ne->getDoubleAttribute ("Time"), ne->getDoubleAttribute ("Duration"), key, (int) ne->getDoubleAttribute ("Velocity", 100) });
    }
    return v;
}

// Places the clip's notes (content time) at arrangement time [start, end), honouring loop settings.
template <typename Emit>
void placeClip (juce::XmlElement* clip, double start, double end, Emit emit)
{
    auto* loop = child (clip, "Loop");
    const double ls = valueOf (loop, "LoopStart"), le = valueOf (loop, "LoopEnd", ls + 4), rel = valueOf (loop, "StartRelative");
    const bool looping = child (loop, "LoopOn") != nullptr && child (loop, "LoopOn")->getStringAttribute ("Value") == "true";
    const auto notes = clipNotes (clip);
    const double from = looping ? ls + rel : ls;
    const double span = end - start;
    if (span <= 0) return;
    if (! looping || le <= ls)
    {
        for (auto& nt : notes)
            if (nt.t >= from && nt.t - from < span) emit (start + nt.t - from, juce::jmin (nt.dur, span - (nt.t - from)), nt.key, nt.vel);
        return;
    }
    const double loopLen = le - ls;
    double pos = start, contentPos = from;
    for (int guard = 0; pos < end && guard < 10000; ++guard)
    {
        const double segEnd = juce::jmin (end, pos + (le - contentPos));
        for (auto& nt : notes)
            if (nt.t >= contentPos && nt.t < le && pos + (nt.t - contentPos) < segEnd)
                emit (pos + nt.t - contentPos, juce::jmin (nt.dur, segEnd - (pos + nt.t - contentPos)), nt.key, nt.vel);
        pos = segEnd;
        contentPos = ls;
        if (loopLen <= 0) break;
    }
}
} // namespace

bool readAls (const juce::MemoryBlock& data, ConvertedSong& out, juce::String& error)
{
    juce::MemoryInputStream raw (data, false);
    juce::GZIPDecompressorInputStream gz (&raw, false, juce::GZIPDecompressorInputStream::gzipFormat);
    const auto xmlText = gz.readEntireStreamAsString();
    auto root = parseXml (xmlText);
    if (root == nullptr || ! root->hasTagName ("Ableton")) { error = T ("Kein Ableton-Live-Set"); return false; }
    auto* set = child (root.get(), "LiveSet");
    auto* tracks = child (set, "Tracks");
    if (tracks == nullptr) { error = T ("Kein Ableton-Live-Set"); return false; }

    constexpr int ppq = 480;
    SmfBuilder b (ppq);
    double bpm = 120;
    if (auto* tempo = findDeep (child (set, "MainTrack") != nullptr ? child (set, "MainTrack") : child (set, "MasterTrack"), "Tempo"))
        bpm = valueOf (tempo, "Manual", 120);
    b.tempo (0, bpm);
    if (auto* ts = findDeep (set, "RemoteableTimeSignature"))
        b.timeSig (0, (int) valueOf (ts, "Numerator", 4), (int) valueOf (ts, "Denominator", 4));

    struct TrackClips { juce::String name; bool drums; juce::Array<juce::XmlElement*> arrangement; std::vector<std::pair<int, juce::XmlElement*>> session; };
    std::vector<TrackClips> list;
    bool anyArrangement = false;
    for (auto* tr : tracks->getChildWithTagNameIterator ("MidiTrack"))
    {
        TrackClips tc;
        if (auto* nm = child (tr, "Name"))
        {
            tc.name = child (nm, "UserName") != nullptr ? child (nm, "UserName")->getStringAttribute ("Value") : juce::String();
            if (tc.name.isEmpty() && child (nm, "EffectiveName") != nullptr) tc.name = child (nm, "EffectiveName")->getStringAttribute ("Value");
        }
        tc.drums = findDeep (tr, "DrumGroupDevice") != nullptr;
        auto* seq = findDeep (tr, "MainSequencer");
        collectDeep (findDeep (child (seq, "ClipTimeable"), "Events"), "MidiClip", tc.arrangement);
        if (auto* slots = child (seq, "ClipSlotList"))
        {
            int scene = 0;
            for (auto* slot : slots->getChildIterator())
            {
                if (auto* clip = findDeep (slot, "MidiClip")) tc.session.push_back ({ scene, clip });
                ++scene;
            }
        }
        list.push_back (std::move (tc));
    }
    // arrangement if it holds most of the material, else the session scenes (live packs, jam sets)
    size_t arrNotes = 0, sesNotes = 0;
    for (auto& tc : list)
    {
        for (auto* c : tc.arrangement) arrNotes += clipNotes (c).size();
        for (auto& sc : tc.session) sesNotes += clipNotes (sc.second).size();
    }
    anyArrangement = arrNotes > 0 && arrNotes * 2 >= sesNotes;

    // session layout: each scene as long as its longest clip
    std::map<int, double> sceneLen;
    if (! anyArrangement)
        for (auto& tc : list)
            for (auto& [scene, clip] : tc.session)
                sceneLen[scene] = juce::jmax (sceneLen[scene], valueOf (clip, "CurrentEnd") - valueOf (clip, "CurrentStart"));
    std::map<int, double> sceneStart;
    double acc = 0;
    for (auto& [scene, len] : sceneLen) { sceneStart[scene] = acc; acc += len; }

    int melodic = 0;
    for (auto& tc : list)
    {
        const int ch = tc.drums ? 9 : (melodic < 9 ? melodic : (melodic % 15) + (melodic % 15 >= 9 ? 1 : 0));
        int tr = -1;
        auto emit = [&] (double t, double dur, int key, int vel) {
            if (tr < 0) tr = b.addTrack (tc.name);
            b.note (tr, ch, (juce::int64) std::llround (t * ppq), (juce::int64) std::llround ((t + juce::jmax (0.001, dur)) * ppq), key, vel);
        };
        if (anyArrangement)
            for (auto* clip : tc.arrangement)
            {
                if (clip->getStringAttribute ("Disabled") == "true") continue;
                placeClip (clip, valueOf (clip, "CurrentStart", clip->getDoubleAttribute ("Time")), valueOf (clip, "CurrentEnd"), emit);
            }
        else
            for (auto& [scene, clip] : tc.session)
                placeClip (clip, sceneStart[scene], sceneStart[scene] + valueOf (clip, "CurrentEnd") - valueOf (clip, "CurrentStart"), emit);
        if (tr >= 0 && ! tc.drums) ++melodic;
    }
    if (! b.hasNotes()) { error = T ("Keine Noten gefunden"); return false; }
    out.format = "Ableton";
    out.smf = b.build();
    out.comment = root->getStringAttribute ("Creator");
    return true;
}
// ============================================================================================ Studio One / Fender Studio Pro SONG
// ZIP: metainfo.xml (title, artist, genre, year), Song/song.xml (TempoMap in seconds per beat, MediaTrack
// mediaType="Music" with MusicPart start/length/offset in beats and clipID), Song/mediapool.xml (MusicClip mediaID → URL
// "media:///Performances/…musicx"). A .musicx clip is Universal Binary JSON: { events: [ { start, length, pitch, velocity } ] }.
namespace
{
struct Ubj
{
    const juce::uint8* p;
    const juce::uint8* end;
    bool ok = true;

    juce::uint8 byte() { if (p >= end) { ok = false; return 0; } return *p++; }
    juce::int64 intOf (juce::uint8 m)
    {
        auto be = [this] (int n) { juce::int64 v = 0; for (int i = 0; i < n; ++i) v = (v << 8) | byte(); return v; };
        switch (m)
        {
            case 'i': return (juce::int8) byte();
            case 'U': return byte();
            case 'I': return (juce::int16) be (2);
            case 'l': return (juce::int32) be (4);
            case 'L': return be (8);
            default: ok = false; return 0;
        }
    }
    juce::var value (juce::uint8 m, int depth = 0)
    {
        if (! ok || depth > 64) { ok = false; return {}; }
        switch (m)
        {
            case 'Z': case 'N': return {};
            case 'T': return true;
            case 'F': return false;
            case 'i': case 'U': case 'I': case 'l': case 'L': return (juce::int64) intOf (m);
            case 'd': { juce::uint32 u = 0; for (int i = 0; i < 4; ++i) u = (u << 8) | byte(); float f; memcpy (&f, &u, 4); return (double) f; }
            case 'D': { juce::uint64 u = 0; for (int i = 0; i < 8; ++i) u = (u << 8) | byte(); double f; memcpy (&f, &u, 8); return f; }
            case 'C': return juce::String::charToString ((juce::juce_wchar) byte());
            case 'S': case 'H':
            {
                const auto len = intOf (byte());
                if (len < 0 || p + len > end) { ok = false; return {}; }
                juce::String s = juce::String::fromUTF8 ((const char*) p, (int) len);
                p += len;
                return s;
            }
            case '[':
            {
                juce::Array<juce::var> arr;
                while (ok && p < end && *p != ']') arr.add (value (byte(), depth + 1));
                byte();
                return arr;
            }
            case '{':
            {
                auto* obj = new juce::DynamicObject();
                juce::var holder (obj);
                while (ok && p < end && *p != '}')
                {
                    const auto len = intOf (byte());
                    if (len < 0 || p + len > end) { ok = false; break; }
                    const juce::String key = juce::String::fromUTF8 ((const char*) p, (int) len);
                    p += len;
                    obj->setProperty (key, value (byte(), depth + 1));
                }
                byte();
                return holder;
            }
            default: ok = false; return {};
        }
    }
};
} // namespace

bool readStudioOne (juce::ZipFile& zip, ConvertedSong& out, juce::String& error)
{
    auto song = parseXml (zipEntryText (zip, "Song/song.xml"));
    if (song == nullptr) { error = T ("Song-Datei beschädigt"); return false; }
    // clip id -> zip path
    std::map<juce::String, juce::String> clipPath;
    if (auto pool = parseXml (zipEntryText (zip, "Song/mediapool.xml")))
    {
        juce::Array<juce::XmlElement*> clips;
        collectDeep (pool.get(), "MusicClip", clips);
        for (auto* c : clips)
            if (auto* url = c->getChildByName ("Url"))
            {
                auto u = url->getStringAttribute ("url");
                if (u.startsWith ("media:///")) clipPath[c->getStringAttribute ("mediaID")] = juce::URL::removeEscapeChars (u.substring (9));
            }
    }
    constexpr int ppq = 480;
    SmfBuilder b (ppq);
    // tempo: seconds per beat; segment start in seconds
    double firstBpm = 0;
    {
        juce::Array<juce::XmlElement*> segs;
        collectDeep (song.get(), "TempoMapSegment", segs);
        double beatPos = 0, lastStart = 0, lastSpb = 0;
        for (auto* s : segs)
        {
            const double spb = s->getDoubleAttribute ("tempo", 0.5), start = s->getDoubleAttribute ("start", 0);
            if (lastSpb > 0) beatPos += (start - lastStart) / lastSpb;
            if (spb > 0) b.tempo ((juce::int64) std::llround (beatPos * ppq), 60.0 / spb);
            if (firstBpm == 0 && spb > 0) firstBpm = 60.0 / spb;
            lastStart = start; lastSpb = spb;
        }
        if (segs.isEmpty()) b.tempo (0, 120.0);
    }
    if (auto* ts = findDeep (song.get(), "TimeSignatureMapSegment"))
        b.timeSig (0, ts->getIntAttribute ("numerator", 4), ts->getIntAttribute ("denominator", 4));

    std::map<juce::String, juce::var> clipCache;
    auto clipEvents = [&] (const juce::String& id) -> juce::var {
        if (auto it = clipCache.find (id); it != clipCache.end()) return it->second;
        juce::var v;
        if (auto pit = clipPath.find (id); pit != clipPath.end())
        {
            int idx = zip.getIndexOfFileName (pit->second, true);
            if (idx < 0)   // names with umlauts may be stored in another code page: compare the ASCII part only
            {
                auto ascii = [] (const juce::String& x) { juce::String r; for (auto c : x) if (c < 128) r += c; return r.toLowerCase(); };
                const auto want = ascii (pit->second);
                for (int i = 0; i < zip.getNumEntries() && idx < 0; ++i)
                    if (ascii (zip.getEntry (i)->filename) == want) idx = i;
            }
            if (idx >= 0)
            {
                const auto mb = zipEntryData (zip, idx);
                Ubj u { static_cast<const juce::uint8*> (mb.getData()), static_cast<const juce::uint8*> (mb.getData()) + mb.getSize() };
                if (mb.getSize() > 0) v = u.value (u.byte());
            }
        }
        return clipCache[id] = v.getProperty ("events", {});
    };

    juce::Array<juce::XmlElement*> tracks;
    collectDeep (song.get(), "MediaTrack", tracks);
    int index = 0;
    for (auto* t : tracks)
    {
        if (t->getStringAttribute ("mediaType") != "Music") continue;
        const int ch = index < 9 ? index : (index % 15) + (index % 15 >= 9 ? 1 : 0);
        int tr = -1;
        juce::Array<juce::XmlElement*> parts;
        collectDeep (t, "MusicPart", parts);
        for (auto* part : parts)
        {
            const double start = part->getDoubleAttribute ("start", 0), length = part->getDoubleAttribute ("length", 0), offset = part->getDoubleAttribute ("offset", 0);
            const int transpose = part->getIntAttribute ("transpose", 0);
            const auto events = clipEvents (part->getStringAttribute ("clipID"));
            if (auto* arr = events.getArray())
                for (auto& e : *arr)
                {
                    const double s = (double) e.getProperty ("start", 0.0), l = (double) e.getProperty ("length", 0.0);
                    if (s < offset || (length > 0 && s >= offset + length)) continue;
                    const double on = start + s - offset;
                    const double off = length > 0 ? juce::jmin (on + juce::jmax (0.001, l), start + length) : on + juce::jmax (0.001, l);
                    const int pitch = (int) e.getProperty ("pitch", -1) + transpose;
                    const double velo = (double) e.getProperty ("velocity", 0.8);
                    if (tr < 0) tr = b.addTrack (t->getStringAttribute ("name"));
                    b.note (tr, ch, (juce::int64) std::llround (on * ppq), (juce::int64) std::llround (off * ppq), pitch, juce::jlimit (1, 127, juce::roundToInt (velo * 127.0)));
                }
        }
        if (tr >= 0) ++index;
    }
    // no placed parts (tracks rendered to audio): lay out the clips left in the media pool, one track per folder
    bool poolOnly = false;
    if (! b.hasNotes())
    {
        std::map<juce::String, std::pair<int, double>> folderTrack;   // folder -> track, next position (beats)
        for (auto& [id, path] : clipPath)
        {
            const auto folder = path.upToLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("/", false, false);
            const auto events = clipEvents (id);
            auto* arr = events.getArray();
            if (arr == nullptr || arr->isEmpty()) continue;
            auto& ft = folderTrack[folder];
            if (ft.first == 0) ft.first = b.addTrack (folder) ;
            const int ch = (int) folderTrack.size() - 1 < 9 ? (int) folderTrack.size() - 1 : ((int) folderTrack.size() % 15) + 1;
            double end = 0;
            for (auto& e : *arr)
            {
                const double s = (double) e.getProperty ("start", 0.0), l = juce::jmax (0.001, (double) e.getProperty ("length", 0.0));
                b.note (ft.first, ch, (juce::int64) std::llround ((ft.second + s) * ppq), (juce::int64) std::llround ((ft.second + s + l) * ppq),
                        (int) e.getProperty ("pitch", -1), juce::jlimit (1, 127, juce::roundToInt ((double) e.getProperty ("velocity", 0.8) * 127.0)));
                end = juce::jmax (end, s + l);
            }
            ft.second += std::ceil (end / 4.0) * 4.0;
            poolOnly = true;
        }
    }
    // metadata
    juce::String generator;
    if (auto meta = parseXml (zipEntryText (zip, "metainfo.xml")))
        for (auto* a : meta->getChildWithTagNameIterator ("Attribute"))
        {
            const auto id = a->getStringAttribute ("id"), v = a->getStringAttribute ("value").trim();
            if (id == "Document:Title") out.title = v;
            else if (id == "Media:Artist") out.artist = v;
            else if (id == "Media:Genre") out.genre = v;
            else if (id == "Media:Year") out.year = v.getIntValue();
            else if (id == "Document:Generator") generator = v.replace ("/", " ");
            else if (id == "Media:Composer") out.composer = v;
        }
    if (! b.hasNotes()) { error = T ("Keine Noten gefunden (nur Audio?)"); return false; }
    out.format = "Studio One";
    out.smf = b.build();
    out.comment = generator;
    if (poolOnly) out.comment = (out.comment.isNotEmpty() ? out.comment + " | " : juce::String()) + juce::String::fromUTF8 ("Notenclips ohne Spur (aus dem Medienpool)");
    return true;
}
}
