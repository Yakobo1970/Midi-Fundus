#include "UltraStar.h"
#include "Lang.h"
#include <algorithm>

namespace ma
{
namespace
{
// ---------------------------------------------------------------- Windows-1252
const juce::juce_wchar kCp1252[32] = {
    0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
    0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };

juce::String fromCp1252 (const juce::uint8* d, size_t n)
{
    juce::String s;
    s.preallocateBytes (n + n / 8);
    for (size_t i = 0; i < n; ++i)
        s += (juce::juce_wchar) (d[i] >= 0x80 && d[i] < 0xA0 ? kCp1252[d[i] - 0x80] : d[i]);
    return s;
}

bool toCp1252 (const juce::String& s, juce::MemoryBlock& out)
{
    for (auto p = s.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        int b = -1;
        if (c < 0x80 || (c >= 0xA0 && c <= 0xFF)) b = (int) c;
        else for (int k = 0; k < 32; ++k) if (kCp1252[k] == c) { b = 0x80 + k; break; }
        if (b < 0) return false;
        const auto byte = (juce::uint8) b;
        out.append (&byte, 1);
    }
    return true;
}

// Decodes the whole file; remembers the encoding for writing back.
juce::String decode (const juce::MemoryBlock& data, bool& utf8, bool& bom)
{
    auto* d = static_cast<const juce::uint8*> (data.getData());
    size_t n = data.getSize();
    bom = n >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF;
    if (bom) { d += 3; n -= 3; }
    const bool validUtf8 = juce::CharPointer_UTF8::isValidString ((const char*) d, (int) n);
    utf8 = bom || validUtf8;
    if (utf8) return juce::String::fromUTF8 ((const char*) d, (int) n);
    return fromCp1252 (d, n);
}

juce::String tagValue (const juce::String& line, juce::String& tag)
{
    const int colon = line.indexOfChar (':');
    if (! line.startsWithChar ('#') || colon < 2) { tag = {}; return {}; }
    tag = line.substring (1, colon).trim().toUpperCase();
    return line.substring (colon + 1).trim();
}

double number (juce::String v) { return v.replaceCharacter (',', '.').trim().getDoubleValue(); }

// English/German/French/own-language names -> German name used in the database.
juce::String languageToGerman (const juce::String& v)
{
    static const char* const map[][2] = {
        { "german", "Deutsch" }, { "deutsch", "Deutsch" }, { "english", "Englisch" }, { "englisch", "Englisch" },
        { "french", "Französisch" }, { "français", "Französisch" }, { "francais", "Französisch" }, { "französisch", "Französisch" },
        { "spanish", "Spanisch" }, { "español", "Spanisch" }, { "spanisch", "Spanisch" }, { "italian", "Italienisch" }, { "italiano", "Italienisch" },
        { "italienisch", "Italienisch" }, { "dutch", "Niederländisch" }, { "nederlands", "Niederländisch" }, { "niederländisch", "Niederländisch" },
        { "portuguese", "Portugiesisch" }, { "português", "Portugiesisch" }, { "portugiesisch", "Portugiesisch" },
        { "swedish", "Schwedisch" }, { "svenska", "Schwedisch" }, { "norwegian", "Norwegisch" }, { "danish", "Dänisch" },
        { "finnish", "Finnisch" }, { "polish", "Polnisch" }, { "polski", "Polnisch" }, { "russian", "Russisch" },
        { "japanese", "Japanisch" }, { "korean", "Koreanisch" }, { "chinese", "Chinesisch" }, { "turkish", "Türkisch" },
        { "latin", "Latein" }, { "czech", "Tschechisch" }, { "hungarian", "Ungarisch" }, { "greek", "Griechisch" },
    };
    const auto low = v.trim().toLowerCase();
    if (low.isEmpty()) return {};
    for (auto& m : map) if (low == juce::String::fromUTF8 (m[0])) return juce::String::fromUTF8 (m[1]);
    return v.trim();
}

// German database name -> English name as UltraStar files use it.
juce::String languageToEnglish (const juce::String& german)
{
    static const char* const map[][2] = {
        { "Deutsch", "German" }, { "Englisch", "English" }, { "Französisch", "French" }, { "Spanisch", "Spanish" },
        { "Italienisch", "Italian" }, { "Niederländisch", "Dutch" }, { "Portugiesisch", "Portuguese" }, { "Schwedisch", "Swedish" },
        { "Norwegisch", "Norwegian" }, { "Dänisch", "Danish" }, { "Finnisch", "Finnish" }, { "Polnisch", "Polish" },
        { "Russisch", "Russian" }, { "Japanisch", "Japanese" }, { "Koreanisch", "Korean" }, { "Chinesisch", "Chinese" },
        { "Türkisch", "Turkish" }, { "Latein", "Latin" }, { "Tschechisch", "Czech" }, { "Ungarisch", "Hungarian" }, { "Griechisch", "Greek" },
    };
    for (auto& m : map) if (german.trim().equalsIgnoreCase (juce::String::fromUTF8 (m[0]))) return m[1];
    return german.trim();
}

// ---------------------------------------------------------------- SMF writer
struct Ev { juce::uint32 tick; int order; juce::MemoryBlock bytes; };

void putVar (juce::MemoryOutputStream& os, juce::uint32 v)
{
    juce::uint8 buf[5];
    int n = 0;
    buf[n++] = (juce::uint8) (v & 0x7F);
    while ((v >>= 7) != 0) buf[n++] = (juce::uint8) (0x80 | (v & 0x7F));
    while (n > 0) os.writeByte ((char) buf[--n]);
}

Ev meta (juce::uint32 tick, int type, const juce::String& text, int order = 0)
{
    juce::MemoryOutputStream os;
    os.writeByte ((char) 0xFF); os.writeByte ((char) type);
    const auto utf = text.toRawUTF8();
    const auto len = (juce::uint32) strlen (utf);
    putVar (os, len);
    os.write (utf, len);
    return { tick, order, os.getMemoryBlock() };
}

Ev raw (juce::uint32 tick, std::initializer_list<int> b, int order)
{
    juce::MemoryBlock mb;
    for (int x : b) { const auto c = (juce::uint8) x; mb.append (&c, 1); }
    return { tick, order, mb };
}

void writeTrack (juce::MemoryOutputStream& file, std::vector<Ev> evs)
{
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });
    juce::MemoryOutputStream tr;
    juce::uint32 last = 0;
    for (auto& e : evs)
    {
        putVar (tr, e.tick - last);
        last = e.tick;
        tr.write (e.bytes.getData(), e.bytes.getSize());
    }
    putVar (tr, 0); tr.writeByte ((char) 0xFF); tr.writeByte (0x2F); tr.writeByte (0);
    file.write ("MTrk", 4);
    file.writeIntBigEndian ((int) tr.getDataSize());
    file.write (tr.getData(), tr.getDataSize());
}
} // namespace

// ============================================================================================ detection
bool looksLikeUltraStar (const void* data, size_t size)
{
    auto* d = static_cast<const char*> (data);
    size_t i = (size >= 3 && (juce::uint8) d[0] == 0xEF && (juce::uint8) d[1] == 0xBB && (juce::uint8) d[2] == 0xBF) ? 3 : 0;
    while (i < size && (d[i] == ' ' || d[i] == '\r' || d[i] == '\n' || d[i] == '\t')) ++i;
    if (i >= size || d[i] != '#') return false;
    // byte search, independent of the text encoding
    auto has = [&] (const char* tag) {
        const auto len = strlen (tag);
        for (size_t k = i; k + len <= juce::jmin<size_t> (size, 4096); ++k)
        {
            size_t m = 0;
            while (m < len && (d[k + m] == tag[m] || (d[k + m] >= 'a' && d[k + m] <= 'z' && d[k + m] - 32 == tag[m]))) ++m;
            if (m == len) return true;
        }
        return false;
    };
    return has ("#TITLE:") && has ("#BPM:");
}

bool isUltraStarFile (const juce::File& f)
{
    if (! f.getFileName().endsWithIgnoreCase (".txt") || f.getSize() > 2 * 1024 * 1024) return false;
    juce::FileInputStream in (f);
    if (! in.openedOk()) return false;
    juce::MemoryBlock mb;
    in.readIntoMemoryBlock (mb, 4096);
    return looksLikeUltraStar (mb.getData(), mb.getSize());
}

// ============================================================================================ parser
bool parseUltraStar (const juce::MemoryBlock& data, UltraStarSong& s)
{
    if (! looksLikeUltraStar (data.getData(), data.getSize())) return false;
    const auto text = decode (data, s.utf8, s.bom);
    s.crlf = text.contains ("\r\n");
    juce::StringArray lines;
    lines.addLines (text);

    int offset = 0, singer = 0;
    bool body = false;
    for (auto& rawLine : lines)
    {
        const auto line = rawLine.trimCharactersAtEnd ("\r");
        if (line.trim().isEmpty()) continue;
        const auto c = line[0];
        if (! body && c == '#')
        {
            juce::String tag;
            const auto v = tagValue (line, tag);
            if (tag == "TITLE") s.title = v;
            else if (tag == "ARTIST") s.artist = v;
            else if (tag == "GENRE") s.genre = v;
            else if (tag == "LANGUAGE") s.language = languageToGerman (v.upToFirstOccurrenceOf (",", false, false));
            else if (tag == "YEAR") s.year = v.getIntValue();
            else if (tag == "COMMENT") s.comment = v;
            else if (tag == "EDITION") s.edition = v;
            else if (tag == "CREATOR") s.creator = v;
            else if (tag == "BPM") s.bpm = number (v);
            else if (tag == "GAP") s.gapMs = number (v);
            else if (tag == "RELATIVE") s.relative = v.equalsIgnoreCase ("yes") || v == "1";
            continue;
        }
        body = true;
        if (c == 'E') break;
        if (c == 'P')
        {
            const int p = line.substring (1).trim().getIntValue();
            singer = p == 2 ? 1 : 0;
            continue;
        }
        auto p = line.getCharPointer();
        ++p;
        auto readInt = [&p] (int& out) {
            while (*p == ' ' || *p == '\t') ++p;
            bool neg = false;
            if (*p == '-') { neg = true; ++p; }
            if (! juce::CharacterFunctions::isDigit (*p)) return false;
            int v = 0;
            while (juce::CharacterFunctions::isDigit (*p)) v = v * 10 + (int) (p.getAndAdvance() - '0');
            out = neg ? -v : v;
            return true;
        };
        if (c == '-')
        {
            int a = 0, b = 0;
            if (! readInt (a)) continue;
            const bool hasB = readInt (b);
            s.breaks.push_back ({ offset + a, singer });
            if (s.relative) offset += hasB ? b : a;
            continue;
        }
        if (c != ':' && c != '*' && c != 'F' && c != 'R' && c != 'G') continue;
        UltraStarSong::Syllable n;
        n.type = (char) c;
        n.singer = singer;
        if (! readInt (n.start) || ! readInt (n.len) || ! readInt (n.pitch)) continue;
        if (*p == ' ' || *p == '\t') ++p;   // exactly one separator – a second blank belongs to the syllable
        n.text = juce::String (p);
        n.start += offset;
        s.notes.push_back (std::move (n));
    }
    if (s.bpm <= 0 || s.notes.empty()) return false;

    // lyrics of the first singer (or of everybody, if only P2 exists), lines at the breaks
    const int who = std::any_of (s.notes.begin(), s.notes.end(), [] (auto& n) { return n.singer == 0; }) ? 0 : 1;
    std::vector<int> br;
    for (auto& b : s.breaks) if (b.second == who) br.push_back (b.first);
    std::sort (br.begin(), br.end());
    juce::String lyr;
    size_t bi = 0;
    for (auto& n : s.notes)
    {
        if (n.singer != who) continue;
        bool newLine = false;
        while (bi < br.size() && br[bi] <= n.start) { ++bi; newLine = true; }
        if (newLine && lyr.isNotEmpty()) lyr = lyr.trimEnd() + "\n";
        auto t = n.text;
        if (t.trim() == "~" || t.trim() == "-") continue;
        if (t.startsWithChar ('~')) t = t.substring (1);
        lyr += t;
    }
    juce::StringArray ls;
    ls.addLines (lyr);
    for (auto& l : ls) l = l.trim();
    ls.removeEmptyStrings();
    s.lyrics = ls.joinIntoString ("\n");
    return true;
}

// ============================================================================================ conversion
juce::MemoryBlock ultraStarToSmf (const UltraStarSong& s)
{
    constexpr int ppq = 480;
    // UltraStar BPM counts sixteenths per minute / 4; keep the shown tempo in a sensible range
    double bpm = s.bpm;
    int perQuarter = 4;   // UltraStar beats per quarter note
    while (bpm > 200.0 && perQuarter < 32) { bpm *= 0.5; perQuarter *= 2; }
    while (bpm < 60.0 && perQuarter > 1) { bpm *= 2.0; perQuarter /= 2; }
    const int tpb = ppq / perQuarter;
    const double gapTicks = s.gapMs / 1000.0 * bpm / 60.0 * ppq;

    int minStart = 0;
    for (auto& n : s.notes) minStart = juce::jmin (minStart, n.start);
    auto tickOf = [&] (int beat) { return (juce::uint32) juce::jmax (0, juce::roundToInt (gapTicks) + (beat - minStart) * tpb); };

    std::vector<Ev> conductor;
    if (s.title.isNotEmpty()) conductor.push_back (meta (0, 0x03, s.title));
    auto tag = [&] (const char* name, const juce::String& v) { if (v.isNotEmpty()) conductor.push_back (meta (0, 0x01, juce::String (name) + ": " + v)); };
    tag ("Artist", s.artist);
    tag ("Genre", s.genre);
    tag ("Year", s.year > 0 ? juce::String (s.year) : juce::String());
    tag ("Language", s.language);
    tag ("Comment", s.comment);
    const auto usPerQuarter = (juce::uint32) juce::roundToInt (60000000.0 / bpm);
    conductor.push_back (raw (0, { 0xFF, 0x51, 3, (int) (usPerQuarter >> 16) & 0xFF, (int) (usPerQuarter >> 8) & 0xFF, (int) usPerQuarter & 0xFF }, 1));
    conductor.push_back (raw (0, { 0xFF, 0x58, 4, 4, 2, 24, 8 }, 1));

    const bool duet = std::any_of (s.notes.begin(), s.notes.end(), [] (auto& n) { return n.singer == 1; });
    std::vector<std::vector<Ev>> tracks (duet ? 2 : 1);
    for (int t = 0; t < (int) tracks.size(); ++t)
    {
        tracks[(size_t) t].push_back (meta (0, 0x03, duet ? (t == 0 ? "Vocals P1" : "Vocals P2") : "Vocals"));
        tracks[(size_t) t].push_back (raw (0, { 0xC0 | t, 53 }, 1));   // GM "Voice Oohs"
    }
    for (auto& b : s.breaks)
        if (b.second < (int) tracks.size()) tracks[(size_t) b.second].push_back (meta (tickOf (b.first), 0x05, "\r", 2));
    for (auto& n : s.notes)
    {
        auto& tr = tracks[(size_t) juce::jmin (n.singer, (int) tracks.size() - 1)];
        const auto on = tickOf (n.start);
        auto txt = n.text;
        if (txt.trim() != "~") tr.push_back (meta (on, 0x05, txt.startsWithChar ('~') ? txt.substring (1) : txt, 3));
        if (n.type == 'F' || n.type == 'R' || n.type == 'G' || n.len <= 0) continue;   // freestyle / rap: no pitch
        const int ch = &tr == &tracks[0] ? 0 : 1;
        const int pitch = juce::jlimit (0, 127, 60 + n.pitch);
        const int vel = n.type == '*' ? 115 : 96;
        const auto off = (juce::uint32) juce::jmax ((int) on + 1, (int) tickOf (n.start + n.len) - 1);
        tr.push_back (raw (on, { 0x90 | ch, pitch, vel }, 4));
        tr.push_back (raw (off, { 0x80 | ch, pitch, 0 }, 0));
    }

    juce::MemoryOutputStream file;
    file.write ("MThd", 4);
    file.writeIntBigEndian (6);
    file.writeShortBigEndian (1);
    file.writeShortBigEndian ((short) (1 + tracks.size()));
    file.writeShortBigEndian (ppq);
    writeTrack (file, std::move (conductor));
    for (auto& t : tracks) writeTrack (file, std::move (t));
    return file.getMemoryBlock();
}

// ============================================================================================ writing
bool writeUltraStarField (const juce::MemoryBlock& data, Field field, const juce::String& rawValue,
                          juce::MemoryBlock& result, juce::String& error)
{
    const char* tag = nullptr;
    auto value = rawValue.replace ("\r", " ").replace ("\n", " ").trim();
    switch (field)
    {
        case Field::title:    tag = "TITLE"; break;
        case Field::artist:   tag = "ARTIST"; break;
        case Field::genre:    tag = "GENRE"; break;
        case Field::year:     tag = "YEAR"; break;
        case Field::comment:  tag = "COMMENT"; break;
        case Field::language: tag = "LANGUAGE"; value = languageToEnglish (value); break;
        default: break;
    }
    if (tag == nullptr) { error = T ("Dieses Feld gibt es im UltraStar-Format nicht"); return false; }
    if ((field == Field::title) && value.isEmpty()) { error = T ("UltraStar-Dateien brauchen einen Titel"); return false; }

    bool utf8 = true, bom = false;
    const auto text = decode (data, utf8, bom);
    const auto eol = text.contains ("\r\n") ? juce::String ("\r\n") : juce::String ("\n");
    juce::StringArray lines;
    lines.addLines (text);
    const bool endsWithEol = text.endsWith ("\n");

    int found = -1, lastHeader = -1;
    for (int i = 0; i < lines.size(); ++i)
    {
        const auto l = lines[i].trimCharactersAtEnd ("\r");
        if (l.trim().isEmpty()) continue;
        if (! l.startsWithChar ('#')) break;
        lastHeader = i;
        juce::String t;
        tagValue (l, t);
        if (t == tag && found < 0) found = i;
    }
    const auto newLine = "#" + juce::String (tag) + ":" + value;
    if (found >= 0)
    {
        if (value.isEmpty()) lines.remove (found);
        else lines.set (found, newLine);
    }
    else if (value.isNotEmpty())
        lines.insert (lastHeader + 1, newLine);

    auto out = lines.joinIntoString (eol);
    if (endsWithEol) out += eol;
    result.reset();
    if (! utf8)
    {
        juce::MemoryBlock cp;
        if (toCp1252 (out, cp)) { result = cp; return true; }
        bom = true;   // characters outside Windows-1252: switch to UTF-8 with BOM (UltraStar reads both)
    }
    if (bom) { const juce::uint8 b[3] = { 0xEF, 0xBB, 0xBF }; result.append (b, 3); }
    result.append (out.toRawUTF8(), strlen (out.toRawUTF8()));
    return true;
}

juce::File ultraStarMidiCopy (const juce::File& txt, const juce::File& folder)
{
    juce::MemoryBlock data;
    UltraStarSong s;
    if (! txt.loadFileAsData (data) || ! parseUltraStar (data, s)) return {};
    auto name = (s.artist.isNotEmpty() ? s.artist + " - " : juce::String()) + (s.title.isNotEmpty() ? s.title : txt.getFileNameWithoutExtension());
    name = juce::File::createLegalFileName (name).substring (0, 120).trim();
    if (name.isEmpty()) name = "UltraStar";
    folder.createDirectory();
    const auto out = folder.getChildFile (name + ".mid");
    const auto smf = ultraStarToSmf (s);
    return out.replaceWithData (smf.getData(), smf.getSize()) ? out : juce::File();
}
} // namespace ma
