#include "MidiData.h"
#include "Lang.h"
#include <algorithm>
#include <map>
#include <unordered_set>

namespace ma
{
// ============================================================================================ helpers
juce::uint32 crc32 (const void* data, size_t size)
{
    static const auto table = [] {
        std::array<juce::uint32, 256> t {};
        for (juce::uint32 i = 0; i < 256; ++i)
        {
            juce::uint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    juce::uint32 c = 0xffffffffu;
    auto* p = static_cast<const juce::uint8*> (data);
    for (size_t i = 0; i < size; ++i) c = table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

namespace
{
inline juce::uint32 mix32 (juce::uint32 h)
{
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
    return h;
}

inline juce::uint32 be32 (const juce::uint8* p) { return ((juce::uint32) p[0] << 24) | ((juce::uint32) p[1] << 16) | ((juce::uint32) p[2] << 8) | p[3]; }
inline juce::uint32 le32 (const juce::uint8* p) { return ((juce::uint32) p[3] << 24) | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[1] << 8) | p[0]; }

bool readVar (const juce::uint8*& p, const juce::uint8* end, juce::uint32& v)
{
    v = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (p >= end) return false;
        const auto b = *p++;
        v = (v << 7) | (b & 0x7f);
        if ((b & 0x80) == 0) return true;
    }
    return true;
}

void writeVar (juce::MemoryOutputStream& os, juce::uint32 v)
{
    juce::uint8 buf[5];
    int n = 0;
    buf[n++] = (juce::uint8) (v & 0x7f);
    while ((v >>= 7) != 0) buf[n++] = (juce::uint8) ((v & 0x7f) | 0x80);
    while (n > 0) os.writeByte ((char) buf[--n]);
}

juce::String decodeText (const juce::uint8* p, size_t n)
{
    while (n > 0 && (p[n - 1] == 0 || p[n - 1] == ' ')) --n;
    bool high = false;
    for (size_t i = 0; i < n; ++i) high |= p[i] >= 0x80;
    if (high && juce::CharPointer_UTF8::isValidString ((const char*) p, (int) n))
        return juce::String::fromUTF8 ((const char*) p, (int) n);
    static const juce::juce_wchar cp1252[32] = { 0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
                                                 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };
    juce::String s;
    s.preallocateBytes (n + 8);
    for (size_t i = 0; i < n; ++i)
    {
        juce::juce_wchar c = p[i];
        if (c >= 0x80 && c < 0xA0) c = cp1252[c - 0x80];
        if (c == 0) continue;
        s += juce::String::charToString (c);
    }
    return s;
}

juce::MemoryBlock encodeText (const juce::String& s)
{
    juce::MemoryBlock mb;
    bool latin = true;
    for (auto c : s) latin &= c < 256;
    if (latin)
        for (auto c : s) { const auto b = (juce::uint8) c; mb.append (&b, 1); }
    else
        mb.append (s.toRawUTF8(), s.getNumBytesAsUTF8());
    return mb;
}

const char* const kMajorNames[12] = { "C", "Des", "D", "Es", "E", "F", "Fis", "G", "As", "A", "B", "H" };
const char* const kMinorNames[12] = { "c", "cis", "d", "es", "e", "f", "fis", "g", "gis", "a", "b", "h" };

juce::String keyName (int tonic, bool minor)
{
    return juce::String (minor ? kMinorNames[tonic] : kMajorNames[tonic]) + (minor ? "-Moll" : "-Dur");
}

// text tags written by MIDIARCHIV (and understood when written by others)
struct TagDef { Field field; const char* names[4]; const char* writeName; };
const TagDef kTags[] = {
    { Field::title,     { "title", "titel", "song", nullptr },          "Title" },
    { Field::artist,    { "artist", "interpret", "performer", nullptr }, "Artist" },
    { Field::composer,  { "composer", "komponist", "composed by", nullptr }, "Composer" },
    { Field::genre,     { "genre", "style", nullptr, nullptr },         "Genre" },
    { Field::year,      { "year", "jahr", nullptr, nullptr },           "Year" },
    { Field::comment,   { "comment", "kommentar", nullptr, nullptr },   "Comment" },
    { Field::language,  { "language", "sprache", nullptr, nullptr },    "Language" },
};

// "Artist: Foo" -> field + value
bool parseTag (const juce::String& text, Field& field, juce::String& value)
{
    const int colon = text.indexOfChar (':');
    if (colon <= 1 || colon > 14) return false;
    const auto key = text.substring (0, colon).trim().toLowerCase();
    for (auto& t : kTags)
        for (auto* n : t.names)
            if (n != nullptr && key == n)
            {
                field = t.field;
                value = text.substring (colon + 1).trim();
                return true;
            }
    return false;
}

bool isGenericName (const juce::String& s)
{
    auto t = s.trim().toLowerCase();
    if (t.isEmpty() || t.containsOnly ("0123456789 -_.#")) return true;
    for (auto* g : { "untitled", "unbenannt", "track", "tempo", "conductor", "sequence", "seq-1", "new song", "song", "midi", "system", "setup", "control", "piano", "drums", "drum", "bass", "melody", "melodie", "lead", "guitar", "gitarre", "strings", "vocal", "vocals", "voice", "kick", "hihat", "snare", "organ", "synth", "brass", "words", "lyrics", "soft karaoke", "* muted lyrics", "muted lyrics", "rhythm", "chords", "pad", "percussion", "perc", "intro", "general midi", "gm", "gs", "xg", "reset", "init", "sax", "flute", "harmony", "backing", "solo", "accomp", "copyright", "untitled sequence", "sequenced by" })
        if (t == g || (t.startsWith (g) && t.substring ((int) strlen (g)).trim().containsOnly ("0123456789 -_.#")))
            return true;
    return false;
}

juce::String afterPhrase (const juce::String& text, const char* phrase)
{
    const int i = text.indexOfIgnoreCase (phrase);
    if (i < 0) return {};
    auto v = text.substring (i + (int) strlen (phrase)).trim();
    for (auto stop : { "\n", "\r", "(", "[", ";", " - ", "  ", "Sequenced", "sequenced", "Arranged", "arranged" })
    {
        const int k = v.indexOf (stop);
        if (k > 0) v = v.substring (0, k);
    }
    v = v.trimCharactersAtEnd (" .,:;-").trim();
    return v.length() >= 3 && v.length() <= 60 ? v : juce::String();
}

juce::String karLanguage (const juce::String& code)
{
    const auto c = code.trim().toUpperCase();
    if (c.startsWith ("ENG")) return juce::String::fromUTF8 ("Englisch");
    if (c.startsWith ("GER") || c.startsWith ("DEU")) return juce::String::fromUTF8 ("Deutsch");
    if (c.startsWith ("ITA")) return juce::String::fromUTF8 ("Italienisch");
    if (c.startsWith ("FRE") || c.startsWith ("FRA")) return juce::String::fromUTF8 ("Französisch");
    if (c.startsWith ("SPA") || c.startsWith ("ESP")) return juce::String::fromUTF8 ("Spanisch");
    if (c.startsWith ("DUT") || c.startsWith ("NED")) return juce::String::fromUTF8 ("Niederländisch");
    if (c.startsWith ("POR")) return juce::String::fromUTF8 ("Portugiesisch");
    return c;
}

struct RawEvent
{
    juce::uint32 tick = 0;
    juce::MemoryBlock bytes;   // complete event without delta (status explicit)
    int metaType = -1;
    juce::String text;
};

bool parseTrackEvents (const juce::uint8* p, const juce::uint8* end, std::vector<RawEvent>& out)
{
    juce::uint32 tick = 0;
    juce::uint8 running = 0;
    while (p < end)
    {
        juce::uint32 delta;
        if (! readVar (p, end, delta)) return false;
        tick += delta;
        if (p >= end) return false;
        juce::uint8 status = *p;
        if (status & 0x80) ++p;
        else if (running != 0) status = running;
        else return false;
        RawEvent e;
        e.tick = tick;
        if (status == 0xFF)
        {
            if (p >= end) return false;
            const auto type = *p++;
            juce::uint32 len;
            if (! readVar (p, end, len) || p + len > end) return false;
            juce::MemoryOutputStream os;
            os.writeByte ((char) 0xFF); os.writeByte ((char) type); writeVar (os, len); os.write (p, len);
            e.metaType = type;
            if (type >= 1 && type <= 7) e.text = decodeText (p, len);
            p += len;
            e.bytes = os.getMemoryBlock();
            out.push_back (std::move (e));
            if (type == 0x2F) return true;
        }
        else if (status == 0xF0 || status == 0xF7)
        {
            juce::uint32 len;
            if (! readVar (p, end, len) || p + len > end) return false;
            juce::MemoryOutputStream os;
            os.writeByte ((char) status); writeVar (os, len); os.write (p, len);
            p += len;
            e.bytes = os.getMemoryBlock();
            out.push_back (std::move (e));
        }
        else
        {
            if (status < 0xF0) running = status;
            const int n = ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) ? 1 : (status >= 0xF0 ? 0 : 2);
            if (p + n > end) return false;
            juce::MemoryOutputStream os;
            os.writeByte ((char) status);
            os.write (p, (size_t) n);
            p += n;
            e.bytes = os.getMemoryBlock();
            out.push_back (std::move (e));
        }
    }
    return true;
}
} // namespace

// ============================================================================================ container
bool extractSmf (const juce::MemoryBlock& data, juce::MemoryBlock& smf, size_t& smfOffset)
{
    auto* d = static_cast<const juce::uint8*> (data.getData());
    const size_t n = data.getSize();
    if (n >= 20 && memcmp (d, "RIFF", 4) == 0 && memcmp (d + 8, "RMID", 4) == 0)
    {
        size_t pos = 12;
        while (pos + 8 <= n)
        {
            const auto len = le32 (d + pos + 4);
            if (memcmp (d + pos, "data", 4) == 0 && pos + 8 + len <= n)
            {
                smf.replaceAll (d + pos + 8, len);
                smfOffset = pos + 8;
                return len >= 14 && memcmp (d + pos + 8, "MThd", 4) == 0;
            }
            pos += 8 + len + (len & 1);
        }
        return false;
    }
    for (size_t i = 0; i + 14 <= n && i <= 512; ++i)   // tolerate MacBinary / junk headers
        if (memcmp (d + i, "MThd", 4) == 0)
        {
            smf.replaceAll (d + i, n - i);
            smfOffset = i;
            return true;
        }
    return false;
}

// ============================================================================================ n-grams
std::vector<juce::uint32> noteGrams (const std::vector<Note>& notes)
{
    // melodic n-grams per (track, channel), skyline, transposition and tempo invariant
    std::map<int, std::vector<const Note*>> voices;
    for (auto& nt : notes) if (nt.ch != 9) voices[nt.track * 16 + nt.ch].push_back (&nt);
    std::vector<juce::uint32> grams;
    for (auto& [k, v] : voices)
    {
        std::vector<std::pair<float, int>> line;   // onset, pitch (highest per onset)
        for (auto* nt : v)
        {
            if (! line.empty() && nt->start - line.back().first < 0.0625f) { line.back().second = juce::jmax (line.back().second, (int) nt->pitch); continue; }
            line.push_back ({ nt->start, nt->pitch });
        }
        if (line.size() < 6) continue;
        for (size_t i = 0; i + 5 < line.size(); ++i)
        {
            juce::uint32 g = 0x9e3779b9u;
            bool allZero = true;
            for (size_t j = 0; j < 5; ++j)
            {
                const int d = juce::jlimit (-12, 12, line[i + j + 1].second - line[i + j].second);
                allZero &= d == 0;
                g = g * 31u + (juce::uint32) (d + 13);
            }
            for (size_t j = 0; j + 1 < 5; ++j)
            {
                const float a = line[i + j + 1].first - line[i + j].first, b = line[i + j + 2].first - line[i + j + 1].first;
                const int cls = b < a * 0.8f ? 0 : (b > a * 1.25f ? 2 : 1);
                g = g * 7u + (juce::uint32) cls;
            }
            if (! allZero) grams.push_back (mix32 (g));
        }
    }
    std::sort (grams.begin(), grams.end());
    grams.erase (std::unique (grams.begin(), grams.end()), grams.end());
    return grams;
}

// ============================================================================================ analysis
MidiInfo analyseMidiFile (const juce::File& f, bool keepNotes)
{
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb))
    {
        MidiInfo m;
        m.error = juce::String::fromUTF8 ("Datei nicht lesbar");
        return m;
    }
    return analyseMidi (mb, keepNotes);
}

MidiInfo analyseMidi (const juce::MemoryBlock& data, bool keepNotes)
{
    MidiInfo m;
    juce::MemoryBlock smf;
    size_t off = 0;
    if (! extractSmf (data, smf, off)) { m.error = "Kein MIDI-Kopf (MThd)"; return m; }
    m.rmi = memcmp (data.getData(), "RIFF", 4) == 0;
    auto* d = static_cast<const juce::uint8*> (smf.getData());
    const size_t n = smf.getSize();
    const auto hlen = be32 (d + 4);
    if (hlen < 6 || 8 + hlen > n) { m.error = juce::String::fromUTF8 ("MIDI-Kopf beschädigt"); return m; }
    m.format = (d[8] << 8) | d[9];
    const int declaredTracks = (d[10] << 8) | d[11];
    int division = (d[12] << 8) | d[13];
    bool smpte = false;
    if (division & 0x8000)
    {
        smpte = true;
        const int fps = 256 - ((division >> 8) & 0xff);
        division = juce::jmax (1, fps * (division & 0xff)) / 2;   // ticks per half second ~ quarter at 120 BPM
    }
    m.ppq = juce::jmax (1, division);

    struct Open { juce::uint32 tick; juce::uint8 vel; };
    struct TickNote { juce::uint32 start, end; juce::uint8 pitch, vel, ch, track; };
    std::vector<TickNote> tnotes;
    std::vector<std::pair<juce::uint32, juce::uint32>> tempos;
    juce::StringArray karHeaders, karSyllables, otherTexts, markers;
    juce::String tagValues[8];
    juce::String trackZeroName, copyright, lyricText;
    std::vector<int> trackNoteCounts;
    bool sawKeySig = false, sawTimeSig = false;
    int karSyllableEvents = 0;

    size_t pos = 8 + hlen;
    int track = 0;
    while (pos + 8 <= n)
    {
        const auto len = be32 (d + pos + 4);
        const bool isTrack = memcmp (d + pos, "MTrk", 4) == 0;
        const size_t avail = juce::jmin ((size_t) len, n - pos - 8);
        if (isTrack)
        {
            std::vector<RawEvent> evs;
            parseTrackEvents (d + pos + 8, d + pos + 8 + avail, evs);   // keep what could be read
            std::array<std::vector<Open>, 16 * 128> open;
            int count = 0;
            juce::uint32 lastTick = 0;
            juce::String name;
            for (auto& e : evs)
            {
                lastTick = e.tick;
                auto* b = static_cast<const juce::uint8*> (e.bytes.getData());
                const auto st = b[0];
                if (e.metaType >= 0)
                {
                    const auto& t = e.text;
                    switch (e.metaType)
                    {
                        case 0x01:
                        {
                            Field f; juce::String v;
                            if (parseTag (t, f, v)) { if (tagValues[(int) f].isEmpty()) tagValues[(int) f] = v; break; }
                            if (t.startsWithChar ('@')) { karHeaders.add (t); break; }
                            if (t.startsWithChar ('/') || t.startsWithChar ('\\')) ++karSyllableEvents;
                            if (karHeaders.size() > 0 || karSyllableEvents > 0) { karSyllables.add (t); break; }
                            if (t.trim().isNotEmpty()) otherTexts.add (t.trim());
                            break;
                        }
                        case 0x02: if (copyright.isEmpty()) copyright = t.trim(); break;
                        case 0x03: if (name.isEmpty()) name = t.trim(); break;
                        case 0x05: lyricText += t; break;
                        case 0x06: if (t.trim().isNotEmpty() && markers.size() < 8) markers.add (t.trim()); break;
                        case 0x51:
                            if (e.bytes.getSize() >= 6)
                                tempos.push_back ({ e.tick, ((juce::uint32) b[3] << 16) | ((juce::uint32) b[4] << 8) | b[5] });
                            break;
                        case 0x58:
                            if (! sawTimeSig && e.bytes.getSize() >= 5) { m.timeSig = juce::String (b[3]) + "/" + juce::String (1 << juce::jmin (6, (int) b[4])); sawTimeSig = true; }
                            break;
                        case 0x59:
                            if (! sawKeySig && e.bytes.getSize() >= 5)
                            {
                                const int sf = (juce::int8) b[3];
                                const bool mi = b[4] != 0;
                                const int tonic = ((sf * 7) % 12 + 12 + (mi ? 9 : 0)) % 12;
                                if (sf >= -7 && sf <= 7) m.keyMeta = keyName (tonic, mi);
                                sawKeySig = true;
                            }
                            break;
                        default: break;
                    }
                    continue;
                }
                if (st >= 0xF0) continue;
                const int ch = st & 0x0F, type = st & 0xF0;
                if (type == 0x90 && e.bytes.getSize() >= 3 && b[2] > 0)
                {
                    open[(size_t) (ch * 128 + (b[1] & 0x7f))].push_back ({ e.tick, b[2] });
                }
                else if ((type == 0x80 || type == 0x90) && e.bytes.getSize() >= 3)
                {
                    auto& o = open[(size_t) (ch * 128 + (b[1] & 0x7f))];
                    if (! o.empty())
                    {
                        tnotes.push_back ({ o.front().tick, e.tick, (juce::uint8) (b[1] & 0x7f), o.front().vel, (juce::uint8) ch, (juce::uint8) juce::jmin (255, track) });
                        o.erase (o.begin());
                        ++count;
                    }
                }
                else if (type == 0xC0 && e.bytes.getSize() >= 2 && ch != 9)
                    m.programs[(size_t) (b[1] & 0x7f)] = true;
            }
            for (int k = 0; k < 16 * 128; ++k)
                for (auto& o : open[(size_t) k])
                {
                    tnotes.push_back ({ o.tick, juce::jmax (o.tick + 1, lastTick), (juce::uint8) (k % 128), o.vel, (juce::uint8) (k / 128), (juce::uint8) juce::jmin (255, track) });
                    ++count;
                }
            if (track == 0) trackZeroName = name;
            m.trackNames.add (name);
            trackNoteCounts.push_back (count);
            ++track;
        }
        if (len > n - pos - 8) break;
        pos += 8 + len;
    }
    m.numTracks = track;
    if (track == 0) { m.error = "Keine Spur gefunden"; return m; }
    juce::ignoreUnused (declaredTracks);

    // ---- tempo map
    std::sort (tempos.begin(), tempos.end());
    if (smpte) tempos.clear();
    if (tempos.empty() || tempos.front().first > 0) tempos.insert (tempos.begin(), { 0u, 500000u });
    {
        juce::uint32 last = tempos.front().second;
        for (auto& t : tempos) { if (t.second != last && t.first > 0) m.tempoChanges = true; last = t.second; }
    }
    m.bpm = 60000000.0 / juce::jmax (1u, tempos.size() > 1 && tempos[0].first == 0 && tempos[1].first == 0 ? tempos[1].second : tempos[0].second);
    {
        // dominant tempo = first explicit one
        for (auto& t : tempos) if (t.first == 0) m.bpm = 60000000.0 / juce::jmax (1u, t.second);
    }
    auto tickToSec = [&] (juce::uint32 tick)
    {
        double sec = 0;
        juce::uint32 prevTick = 0, us = tempos.front().second;
        for (auto& t : tempos)
        {
            if (t.first >= tick) break;
            sec += (double) (t.first - prevTick) * us / (1e6 * m.ppq);
            prevTick = t.first; us = t.second;
        }
        return sec + (double) (tick - prevTick) * us / (1e6 * m.ppq);
    };

    // ---- notes
    std::sort (tnotes.begin(), tnotes.end(), [] (const TickNote& a, const TickNote& b) { return a.start != b.start ? a.start < b.start : a.pitch > b.pitch; });
    m.noteCount = (int) tnotes.size();
    juce::uint32 endTick = 0;
    std::array<double, 12> pc {};
    for (auto& t : tnotes)
    {
        endTick = juce::jmax (endTick, t.end);
        m.channelMask |= (juce::uint16) (1u << t.ch);
        if (t.ch == 9) { ++m.drumNotes; continue; }
        pc[(size_t) (t.pitch % 12)] += juce::jlimit (0.125, 4.0, (double) (t.end - t.start) / m.ppq);
    }
    for (int c = 0; c < 16; ++c) m.channelCount += (m.channelMask >> c) & 1;
    m.seconds = tickToSec (endTick);

    std::vector<Note> notes;
    notes.reserve (tnotes.size());
    for (auto& t : tnotes)
        notes.push_back ({ (float) t.start / m.ppq, (float) (t.end - t.start) / m.ppq, t.pitch, t.vel, t.ch, t.track });

    // ---- key / scale (Krumhansl-Kessler)
    {
        static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        double total = 0;
        for (auto v : pc) total += v;
        if (total > 0)
        {
            double maxV = 0;
            for (auto v : pc) maxV = juce::jmax (maxV, v);
            for (int i = 0; i < 12; ++i) m.pitchClasses[(size_t) i] = (juce::uint8) juce::roundToInt (255.0 * pc[(size_t) i] / maxV);
            auto corr = [&] (const double* prof, int tonic)
            {
                double mx = 0, my = 0;
                for (int i = 0; i < 12; ++i) { mx += pc[(size_t) i]; my += prof[i]; }
                mx /= 12; my /= 12;
                double sxy = 0, sxx = 0, syy = 0;
                for (int i = 0; i < 12; ++i)
                {
                    const double x = pc[(size_t) ((i + tonic) % 12)] - mx, y = prof[i] - my;
                    sxy += x * y; sxx += x * x; syy += y * y;
                }
                return sxx > 0 ? sxy / std::sqrt (sxx * syy) : 0.0;
            };
            double best = -2; int bt = 0; bool bm = false;
            for (int t = 0; t < 12; ++t)
            {
                const double a = corr (maj, t), b = corr (mnr, t);
                if (a > best) { best = a; bt = t; bm = false; }
                if (b > best) { best = b; bt = t; bm = true; }
            }
            m.keyConfidence = (float) best;
            m.key = keyName (bt, bm) + (best < 0.6 ? "?" : "");

            // scale: pitch classes carrying >= 3 % of the weight
            int used = 0; juce::uint16 set = 0;
            for (int i = 0; i < 12; ++i)
                if (pc[(size_t) i] >= 0.03 * total) { ++used; set |= (juce::uint16) (1u << i); }
            auto rot = [] (std::initializer_list<int> steps, int tonic)
            {
                juce::uint16 s = 0;
                for (int k : steps) s |= (juce::uint16) (1u << ((k + tonic) % 12));
                return s;
            };
            const int majTonic = bm ? (bt + 3) % 12 : bt;
            const auto diat = rot ({ 0, 2, 4, 5, 7, 9, 11 }, majTonic);
            const auto penta = rot ({ 0, 2, 4, 7, 9 }, majTonic);
            const auto harm = rot ({ 0, 2, 3, 5, 7, 8, 11 }, bt);
            const auto blues = rot ({ 0, 3, 5, 6, 7, 10 }, bt);
            if (used <= 5 && (set & ~penta) == 0) m.scale = "Pentatonik";
            else if ((set & ~diat) == 0) m.scale = "Diatonisch";
            else if (bm && (set & ~harm) == 0) m.scale = "Harmonisch Moll";
            else if ((set & ~(blues | diat)) == 0 && (set & blues) == blues) m.scale = "Blues";
            else if (used >= 10) m.scale = "Chromatisch";
            else m.scale = "Erweitert";
            m.scale << " · " << used;
        }
    }

    // ---- instruments summary
    {
        juce::StringArray names;
        for (int p = 0; p < 128 && names.size() < 4; ++p) if (m.programs[(size_t) p]) names.add (gmProgramName (p));
        int more = 0;
        for (int p = 0; p < 128; ++p) more += m.programs[(size_t) p] ? 1 : 0;
        m.instruments = names.joinIntoString (", ");
        if (more > names.size()) m.instruments << " +" << (more - names.size());
        if (m.drumNotes > 0) m.instruments = (m.instruments.isEmpty() ? juce::String ("Drums") : m.instruments + ", Drums");
    }

    // ---- metadata
    m.karaoke = karHeaders.size() > 0 || karSyllableEvents > 4;
    {
        int karT = 0;
        for (auto& h : karHeaders)
        {
            const auto tag = h.substring (1, 2).toUpperCase();
            const auto v = h.substring (2).trim();
            if (tag == "T") { if (karT == 0 && m.title.isEmpty()) m.title = v; else if (karT == 1 && m.artist.isEmpty()) m.artist = v; ++karT; }
            else if (tag == "L" && m.language.isEmpty()) m.language = karLanguage (v);
            else if (tag == "I" && v.isNotEmpty()) otherTexts.add (v);
        }
    }
    if (lyricText.isNotEmpty()) m.lyrics = lyricText;
    else if (m.karaoke)
    {
        juce::String s;
        for (auto& t : karSyllables)
        {
            if (t.startsWithChar ('/') || t.startsWithChar ('\\')) s << "\n" << t.substring (1);
            else s << t;
        }
        m.lyrics = s.trim();
    }
    m.lyrics = m.lyrics.replace ("\r\n", "\n").replace ("\r", "\n").trim();

    const bool trackZeroConductor = m.format == 0 || (trackNoteCounts.size() > 1 && trackNoteCounts[0] == 0);
    if (tagValues[(int) Field::title].isNotEmpty()) m.title = tagValues[(int) Field::title];
    else if (m.title.isEmpty() && trackZeroConductor && ! isGenericName (trackZeroName)) m.title = trackZeroName;
    m.copyright = copyright;
    // "TITLE            ;Composer / Composer" (Tune 1000 and others)
    if (const int semi = m.title.indexOfChar (';'); semi > 0 && tagValues[(int) Field::title].isEmpty())
    {
        const auto after = m.title.substring (semi + 1).trim();
        m.title = m.title.substring (0, semi).trim();
        if (after.isNotEmpty() && tagValues[(int) Field::composer].isEmpty() && m.composer.isEmpty()) m.composer = after;
    }

    if (tagValues[(int) Field::artist].isNotEmpty()) m.artist = tagValues[(int) Field::artist];
    if (tagValues[(int) Field::composer].isNotEmpty()) m.composer = tagValues[(int) Field::composer];
    m.genre = tagValues[(int) Field::genre];
    m.year = tagValues[(int) Field::year].getIntValue();
    if (tagValues[(int) Field::language].isNotEmpty()) m.language = tagValues[(int) Field::language];

    juce::StringArray all (otherTexts);
    all.addArray (m.trackNames);
    all.add (copyright);
    for (auto& t : all)
    {
        if (m.composer.isEmpty())
            for (auto* ph : { "composed by", "music by", "words and music by", "written by", "komponist:", "musik:", "composer " })
                if (auto v = afterPhrase (t, ph); v.isNotEmpty()) { m.composer = v; break; }
        if (m.artist.isEmpty())
            for (auto* ph : { "performed by", "artist:", "interpret:", "recorded by" })
                if (auto v = afterPhrase (t, ph); v.isNotEmpty()) { m.artist = v; break; }
    }

    if (tagValues[(int) Field::comment].isNotEmpty()) m.comment = tagValues[(int) Field::comment];
    else
    {
        juce::StringArray parts (otherTexts);
        parts.removeDuplicates (true);
        for (auto& mk : markers) parts.add ("Marker: " + mk);
        auto c = parts.joinIntoString (" | ");
        m.comment = c.length() > 400 ? c.substring (0, 400) + juce::String::fromUTF8 ("…") : c;
    }
    m.texts = otherTexts;
    if (m.language.isEmpty() && m.lyrics.length() > 60) m.language = detectLanguage (m.lyrics);

    // RIFF INFO (RMI)
    if (m.rmi)
    {
        auto* r = static_cast<const juce::uint8*> (data.getData());
        const size_t rn = data.getSize();
        for (size_t p = 12; p + 12 <= rn;)
        {
            const auto len = le32 (r + p + 4);
            if (memcmp (r + p, "LIST", 4) == 0 && memcmp (r + p + 8, "INFO", 4) == 0)
            {
                for (size_t q = p + 12; q + 8 <= juce::jmin (rn, (size_t) (p + 8 + len));)
                {
                    const auto l2 = le32 (r + q + 4);
                    if (q + 8 + l2 > rn) break;
                    const auto v = decodeText (r + q + 8, l2).trim();
                    const juce::String id ((const char*) r + q, 4);
                    if (id == "INAM" && m.title.isEmpty()) m.title = v;
                    else if (id == "IART" && m.artist.isEmpty()) m.artist = v;
                    else if (id == "ICOP" && m.copyright.isEmpty()) m.copyright = v;
                    else if (id == "IGNR" && m.genre.isEmpty()) m.genre = v;
                    else if (id == "ICRD" && m.year == 0) m.year = v.getIntValue();
                    else if (id == "ICMT" && m.comment.isEmpty()) m.comment = v;
                    q += 8 + l2 + (l2 & 1);
                }
            }
            p += 8 + len + (len & 1);
        }
    }

    // ---- fingerprints
    {
        juce::uint64 h = 1469598103934665603ull;
        auto feed = [&h] (juce::uint32 v) { for (int i = 0; i < 4; ++i) { h ^= (v >> (i * 8)) & 0xff; h *= 1099511628211ull; } };
        for (auto& nt : notes) { feed ((juce::uint32) juce::roundToInt (nt.start * 48.0f)); feed ((juce::uint32) nt.pitch | (nt.ch == 9 ? 0x100u : 0u)); }
        m.noteHash = notes.empty() ? 0 : h;
    }
    {
        auto grams = noteGrams (notes);
        m.gramCount = (int) grams.size();
        m.sketch.assign (grams.begin(), grams.begin() + juce::jmin ((int) grams.size(), kSketch));
    }
    {
        // mountain: note onsets over time, stacked per track (largest tracks first)
        std::vector<std::pair<int, int>> order;   // count, track
        for (int t = 0; t < (int) trackNoteCounts.size(); ++t) if (trackNoteCounts[(size_t) t] > 0) order.push_back ({ trackNoteCounts[(size_t) t], t });
        std::sort (order.begin(), order.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        std::vector<int> slot (trackNoteCounts.size(), kMountTracks - 1);
        for (int i = 0; i < juce::jmin ((int) order.size(), kMountTracks); ++i) slot[(size_t) order[(size_t) i].second] = i;
        // keep original track order inside the stack for a stable look
        float endBeat = 0;
        for (auto& nt : notes) endBeat = juce::jmax (endBeat, nt.start + nt.len);
        std::array<float, kMountBins * kMountTracks> acc {};
        if (endBeat > 0)
            for (auto& nt : notes)
            {
                const int bin = juce::jlimit (0, kMountBins - 1, (int) (nt.start / endBeat * kMountBins));
                acc[(size_t) (bin * kMountTracks + slot[(size_t) nt.track])] += 1.0f;
            }
        float mx = 0;
        for (int b = 0; b < kMountBins; ++b)
        {
            float s = 0;
            for (int t = 0; t < kMountTracks; ++t) s += acc[(size_t) (b * kMountTracks + t)];
            mx = juce::jmax (mx, s);
        }
        if (mx > 0)
            for (size_t i = 0; i < acc.size(); ++i) m.mountain[i] = (juce::uint8) juce::roundToInt (juce::jmin (255.0f, 255.0f * acc[i] / mx));
    }

    if (keepNotes) m.notes = std::move (notes);
    m.ok = true;
    return m;
}

// ============================================================================================ writing
juce::File backupFolder()
{
    if (auto env = juce::SystemStats::getEnvironmentVariable ("MIDIARCHIV_BACKUP", {}); env.isNotEmpty()) return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MIDIARCHIV").getChildFile ("Sicherung");
}

bool writeMidiField (const juce::File& f, Field field, const juce::String& rawValue, juce::String& error)
{
    const auto value = rawValue.replace ("\n", " ").trim();
    juce::MemoryBlock data;
    if (! f.loadFileAsData (data)) { error = T ("Datei nicht lesbar"); return false; }
    const auto before = analyseMidi (data);
    if (! before.ok) { error = before.error; return false; }
    juce::MemoryBlock smf;
    size_t off = 0;
    extractSmf (data, smf, off);
    auto* d = static_cast<const juce::uint8*> (smf.getData());
    const size_t n = smf.getSize();
    const auto hlen = be32 (d + 4);

    // locate the first MTrk
    size_t pos = 8 + hlen, t0 = 0, t0len = 0;
    while (pos + 8 <= n)
    {
        const auto len = be32 (d + pos + 4);
        if (memcmp (d + pos, "MTrk", 4) == 0) { t0 = pos; t0len = juce::jmin ((size_t) len, n - pos - 8); break; }
        pos += 8 + len;
    }
    if (t0 == 0) { error = "Keine Spur"; return false; }
    std::vector<RawEvent> evs;
    if (! parseTrackEvents (d + t0 + 8, d + t0 + 8 + t0len, evs)) { error = T ("Spur 1 ist beschädigt – Schreiben abgebrochen"); return false; }

    const bool conductor = before.format == 0 || (before.trackNames.size() > 1 && [&] {
        for (auto& e : evs) { auto st = ((const juce::uint8*) e.bytes.getData())[0]; if ((st & 0xF0) == 0x90) return false; }
        return true; }());

    const TagDef* tag = nullptr;
    for (auto& t : kTags) if (t.field == field) tag = &t;

    // remove old values
    std::vector<RawEvent> out;
    bool removedSeqName = false;
    for (auto& e : evs)
    {
        if (e.metaType == 0x01 && tag != nullptr)
        {
            Field ff; juce::String v;
            if (parseTag (e.text, ff, v) && ff == field) continue;
        }
        if (field == Field::copyright && e.metaType == 0x02) continue;
        if (field == Field::keySignature && e.metaType == 0x59 && e.tick == 0) continue;
        if (field == Field::title && conductor && e.metaType == 0x03 && e.tick == 0 && ! removedSeqName) { removedSeqName = true; continue; }
        out.push_back (std::move (e));
    }

    auto makeMeta = [] (int type, const juce::String& text)
    {
        RawEvent e;
        e.metaType = type;
        const auto enc = encodeText (text);
        juce::MemoryOutputStream os;
        os.writeByte ((char) 0xFF); os.writeByte ((char) type); writeVar (os, (juce::uint32) enc.getSize()); os.write (enc.getData(), enc.getSize());
        e.bytes = os.getMemoryBlock();
        return e;
    };
    std::vector<RawEvent> ins;
    if (field == Field::keySignature && value.isNotEmpty())
    {
        int tonic = 0; bool minor = false;
        if (! parseKeyName (value, tonic, minor)) { error = T ("Tonart nicht verstanden: ") + value; return false; }
        static const int sfOfMajor[12] = { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };
        const int sf = sfOfMajor[(minor ? tonic + 3 : tonic) % 12];
        RawEvent e;
        e.metaType = 0x59;
        const juce::uint8 b[5] = { 0xFF, 0x59, 0x02, (juce::uint8) (juce::int8) sf, (juce::uint8) (minor ? 1 : 0) };
        e.bytes.append (b, 5);
        ins.push_back (std::move (e));
    }
    else if (value.isNotEmpty())
    {
        if (field == Field::copyright) ins.push_back (makeMeta (0x02, value));
        else
        {
            if (field == Field::title && conductor) ins.push_back (makeMeta (0x03, value));
            ins.push_back (makeMeta (0x01, juce::String (tag->writeName) + ": " + value));
        }
    }
    // insert after an existing tick-0 sequence name / copyright, otherwise at the very start
    size_t at = 0;
    while (at < out.size() && out[at].tick == 0 && (out[at].metaType == 0x03 || out[at].metaType == 0x02 || out[at].metaType == 0x01)) ++at;
    if (field == Field::title || field == Field::copyright) at = 0;
    if (field == Field::keySignature)   // after tempo / time signature at tick 0
        while (at < out.size() && out[at].tick == 0 && out[at].metaType >= 0 && out[at].metaType != 0x2F) ++at;
    out.insert (out.begin() + (long) at, std::make_move_iterator (ins.begin()), std::make_move_iterator (ins.end()));
    if (out.empty() || out.back().metaType != 0x2F)
    {
        RawEvent eot; eot.tick = out.empty() ? 0 : out.back().tick; eot.metaType = 0x2F;
        const juce::uint8 b[3] = { 0xFF, 0x2F, 0x00 };
        eot.bytes.append (b, 3);
        out.push_back (std::move (eot));
    }

    juce::MemoryBlock track;
    {
        juce::MemoryOutputStream os (track, false);
        juce::uint32 last = 0;
        for (auto& e : out)
        {
            writeVar (os, e.tick - juce::jmin (e.tick, last));
            last = juce::jmax (last, e.tick);
            os.write (e.bytes.getData(), e.bytes.getSize());
        }
    }
    juce::MemoryBlock newSmf;
    {
        juce::MemoryOutputStream os (newSmf, false);
        os.write (d, t0);
        os.write ("MTrk", 4);
        os.writeIntBigEndian ((int) track.getSize());
        os.write (track.getData(), track.getSize());
        const size_t rest = t0 + 8 + t0len;
        if (rest < n) os.write (d + rest, n - rest);
    }
    juce::MemoryBlock result;
    if (before.rmi)
    {
        auto* r = static_cast<const juce::uint8*> (data.getData());
        const size_t rn = data.getSize();
        juce::MemoryOutputStream os (result, false);
        os.write ("RIFF", 4); os.writeInt (0); os.write ("RMID", 4);
        for (size_t p = 12; p + 8 <= rn;)
        {
            const auto len = le32 (r + p + 4);
            if (memcmp (r + p, "data", 4) == 0)
            {
                os.write ("data", 4); os.writeInt ((int) newSmf.getSize()); os.write (newSmf.getData(), newSmf.getSize());
                if (newSmf.getSize() & 1) os.writeByte (0);
            }
            else
            {
                const size_t l = juce::jmin ((size_t) len + (len & 1), rn - p - 8);
                os.write (r + p, 8 + l);
            }
            p += 8 + len + (len & 1);
        }
        os.flush();
        const auto total = (juce::uint32) (result.getSize() - 8);
        auto* w = static_cast<juce::uint8*> (result.getData());
        w[4] = (juce::uint8) total; w[5] = (juce::uint8) (total >> 8); w[6] = (juce::uint8) (total >> 16); w[7] = (juce::uint8) (total >> 24);
    }
    else
    {
        result.append (data.getData(), off);   // keep a possible leading header
        result.append (newSmf.getData(), newSmf.getSize());
    }

    // verify: same music, new value readable
    const auto after = analyseMidi (result);
    if (! after.ok || after.noteHash != before.noteHash || after.noteCount != before.noteCount)
    {
        error = T ("Prüfung nach dem Schreiben fehlgeschlagen – Datei unverändert");
        return false;
    }

    // one-time backup of the original
    auto bdir = backupFolder();
    bdir.createDirectory();
    const auto bfile = bdir.getChildFile (juce::String::toHexString ((juce::int64) juce::DefaultHashFunctions::generateHash (f.getFullPathName().toLowerCase(), INT_MAX)) + "_" + f.getFileName());
    if (! bfile.existsAsFile()) f.copyFileTo (bfile);

    juce::TemporaryFile tmp (f);
    if (! tmp.getFile().replaceWithData (result.getData(), result.getSize()) || ! tmp.overwriteTargetFileWithTemporary())
    {
        error = T ("Datei konnte nicht geschrieben werden (schreibgeschützt?)");
        return false;
    }
    return true;
}

// ============================================================================================ musical helpers
juce::String gmProgramName (int p)
{
    static const char* const names[128] = {
        "Acoustic Grand Piano", "Bright Piano", "Electric Grand", "Honky-tonk", "E-Piano 1", "E-Piano 2", "Harpsichord", "Clavinet",
        "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
        "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
        "Nylon Guitar", "Steel Guitar", "Jazz Guitar", "Clean Guitar", "Muted Guitar", "Overdrive Guitar", "Distortion Guitar", "Guitar Harmonics",
        "Acoustic Bass", "Finger Bass", "Pick Bass", "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
        "Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Harp", "Timpani",
        "String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2", "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
        "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
        "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
        "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
        "Square Lead", "Saw Lead", "Calliope", "Chiff Lead", "Charang", "Voice Lead", "Fifths Lead", "Bass + Lead",
        "New Age Pad", "Warm Pad", "Polysynth", "Choir Pad", "Bowed Pad", "Metallic Pad", "Halo Pad", "Sweep Pad",
        "Rain", "Soundtrack", "Crystal", "Atmosphere", "Brightness", "Goblins", "Echoes", "Sci-Fi",
        "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai",
        "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
        "Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone", "Helicopter", "Applause", "Gunshot" };
    return names[juce::jlimit (0, 127, p)];
}

juce::String keyNameOf (int tonic, bool minor) { return keyName ((tonic % 12 + 12) % 12, minor); }

bool parseKeyName (const juce::String& text, int& tonic, bool& minor)
{
    auto s = text.toLowerCase().removeCharacters ("?- ").trim();
    int mode = -1;   // -1 unknown, 0 major, 1 minor
    for (auto* w : { "dur", "major", "maj" }) if (s.endsWith (w)) { mode = 0; s = s.dropLastCharacters ((int) strlen (w)); break; }
    if (mode < 0) for (auto* w : { "moll", "minor", "min" }) if (s.endsWith (w)) { mode = 1; s = s.dropLastCharacters ((int) strlen (w)); break; }
    if (s.isEmpty()) return false;
    static const int base[] = { 9, 10, 0, 2, 4, 5, 7, 11 };   // a b c d e f g h
    const auto L = s[0];
    if (L < 'a' || L > 'h') return false;
    int pc = base[L - 'a'];
    auto rest = s.substring (1);
    if (rest.startsWith ("is") || rest.startsWith ("#")) { pc += 1; rest = rest.substring (rest.startsWith ("#") ? 1 : 2); }
    else if (rest.startsWith ("es")) { pc -= 1; rest = rest.substring (2); }
    else if (rest.startsWith ("s") && (L == 'a' || L == 'e')) { pc -= 1; rest = rest.substring (1); }
    else if (rest.startsWith ("b") && L != 'b') { pc -= 1; rest = rest.substring (1); }
    if (rest == "m") { if (mode < 0) mode = 1; rest = {}; }
    if (rest.isNotEmpty()) return false;
    if (mode < 0) mode = juce::CharacterFunctions::isLowerCase (text.trim()[0]) ? 1 : 0;   // German: lower case = minor
    tonic = (pc % 12 + 12) % 12;
    minor = mode == 1;
    return true;
}

juce::String noteName (int pitch)
{
    static const char* const n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "H" };
    return juce::String (n[pitch % 12]) + juce::String (pitch / 12 - 1);
}

std::vector<Note> extractMelody (const std::vector<Note>& notes, int maxNotes)
{
    std::map<int, std::vector<const Note*>> voices;
    for (auto& nt : notes) if (nt.ch != 9) voices[nt.track * 16 + nt.ch].push_back (&nt);
    double bestScore = -1; int bestKey = -1;
    for (auto& [k, v] : voices)
    {
        if (v.size() < 8) continue;
        int poly = 0; double avg = 0;
        for (size_t i = 0; i < v.size(); ++i)
        {
            avg += v[i]->pitch;
            if (i > 0 && v[i]->start - v[i - 1]->start < 0.0625f) ++poly;
        }
        avg /= (double) v.size();
        const double mono = 1.0 - (double) poly / (double) v.size();
        const double range = (avg >= 57 && avg <= 84) ? 1.0 : 0.4;
        const double s = std::log ((double) v.size()) * mono * mono * range;
        if (s > bestScore) { bestScore = s; bestKey = k; }
    }
    std::vector<Note> mel;
    if (bestKey < 0) return mel;
    for (auto* nt : voices[bestKey])
    {
        if (! mel.empty() && nt->start - mel.back().start < 0.0625f) { if (nt->pitch > mel.back().pitch) mel.back() = *nt; continue; }
        mel.push_back (*nt);
        if ((int) mel.size() >= maxNotes) break;
    }
    return mel;
}

juce::String parsonsCode (const std::vector<Note>& mel)
{
    juce::String s ("*");
    for (size_t i = 1; i < mel.size(); ++i)
        s << (mel[i].pitch > mel[i - 1].pitch ? "U" : (mel[i].pitch < mel[i - 1].pitch ? "D" : "R"));
    return s;
}

float sketchSimilarity (const std::vector<juce::uint32>& a, const std::vector<juce::uint32>& b)
{
    if (a.empty() || b.empty()) return 0.0f;
    const size_t k = juce::jmin (a.size(), b.size());
    size_t i = 0, j = 0, taken = 0, both = 0;
    while (taken < k && (i < a.size() || j < b.size()))
    {
        if (j >= b.size() || (i < a.size() && a[i] < b[j])) ++i;
        else if (i >= a.size() || b[j] < a[i]) ++j;
        else { ++both; ++i; ++j; }
        ++taken;
    }
    return (float) both / (float) k;
}

juce::String detectLanguage (const juce::String& text)
{
    struct L { const char* name; const char* words; };
    static const L langs[] = {
        { "Englisch", " the you and i to me my love it is in of that your on be all what don't i'm we can for with" },
        { "Deutsch", " ich du und die der das nicht ist ein eine mich mir dich wir sie es zu auf mit für kein noch" },
        { "Italienisch", " il che di e la non sei io tu per un una mi ti con amore sono come ma più" },
        { "Französisch", " je tu et le la les de des que pas est un une moi toi pour dans mon ma qui" },
        { "Spanisch", " el la que y de no es un una yo tu mi te por con amor como mas para" },
        { "Niederländisch", " ik je en de het een niet is van dat mijn jij we zijn op met voor" },
    };
    juce::StringArray words;
    words.addTokens (text.toLowerCase().replaceCharacters (",.!?\"()\n\r-", "           "), " ", "");
    words.removeEmptyStrings();
    if (words.size() < 12) return {};
    int best = -1, bestHits = 0, second = 0;
    for (int l = 0; l < (int) std::size (langs); ++l)
    {
        const juce::String list = juce::String::fromUTF8 (langs[l].words) + " ";
        int hits = 0;
        for (auto& w : words) if (list.contains (" " + w + " ")) ++hits;
        if (hits > bestHits) { second = bestHits; bestHits = hits; best = l; }
        else if (hits > second) second = hits;
    }
    if (best < 0 || bestHits < 4 || bestHits < second * 3 / 2) return {};
    return juce::String::fromUTF8 (langs[best].name);
}
} // namespace ma
