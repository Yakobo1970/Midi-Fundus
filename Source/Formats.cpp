#include "Formats.h"
#include "SmfBuilder.h"
#include "UltraStar.h"
#include "Lang.h"

namespace ma
{
namespace
{
juce::String ext (const juce::String& fileName) { return fileName.fromLastOccurrenceOf (".", false, false).toLowerCase(); }

bool startsWith (const juce::MemoryBlock& d, const char* magic, size_t at = 0)
{
    const auto n = strlen (magic);
    return d.getSize() >= at + n && memcmp (static_cast<const char*> (d.getData()) + at, magic, n) == 0;
}

juce::MemoryBlock readHead (const juce::File& f, int bytes)
{
    juce::MemoryBlock mb;
    juce::FileInputStream in (f);
    if (in.openedOk()) in.readIntoMemoryBlock (mb, bytes);
    return mb;
}

bool headContains (const juce::MemoryBlock& head, const char* text)
{
    const auto n = strlen (text);
    auto* d = static_cast<const char*> (head.getData());
    for (size_t i = 0; i + n <= head.getSize(); ++i)
        if (memcmp (d + i, text, n) == 0) return true;
    return false;
}

bool looksLikeMusicXml (const juce::MemoryBlock& head) { return headContains (head, "<score-partwise") || headContains (head, "<score-timewise"); }
bool looksLikeMuseScore (const juce::MemoryBlock& head) { return headContains (head, "<museScore"); }

bool looksLikeAbc (const juce::String& t)
{
    juce::StringArray lines;
    lines.addLines (t.substring (0, 6000));
    bool x = false, k = false;
    for (auto& l : lines)
    {
        if (l.startsWith ("X:")) x = true;
        if (x && l.startsWith ("K:")) k = true;
    }
    return x && k;
}
} // namespace

// ============================================================================================ helpers
namespace fmt
{
juce::String decodeText (const void* data, size_t size)
{
    auto* d = static_cast<const juce::uint8*> (data);
    if (size >= 2 && ((d[0] == 0xFF && d[1] == 0xFE) || (d[0] == 0xFE && d[1] == 0xFF)))
        return juce::String::createStringFromData (data, (int) size);   // UTF-16 with BOM
    if (size >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) { d += 3; size -= 3; }
    if (juce::CharPointer_UTF8::isValidString ((const char*) d, (int) size)) return juce::String::fromUTF8 ((const char*) d, (int) size);
    static const juce::juce_wchar cp[32] = {
        0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
        0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };
    juce::String s;
    s.preallocateBytes (size + size / 8);
    for (size_t i = 0; i < size; ++i) s += (juce::juce_wchar) (d[i] >= 0x80 && d[i] < 0xA0 ? cp[d[i] - 0x80] : d[i]);
    return s;
}

std::unique_ptr<juce::XmlElement> parseXml (const juce::String& text)
{
    juce::XmlDocument doc (text);
    doc.setEmptyTextElementsIgnored (true);
    return doc.getDocumentElement();
}

juce::MemoryBlock zipEntryData (juce::ZipFile& zip, int index)
{
    juce::MemoryBlock mb;
    if (auto* e = zip.getEntry (index))
        if (std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (*e)); in != nullptr)
            in->readIntoMemoryBlock (mb, (juce::ssize_t) juce::jmin<juce::int64> (e->uncompressedSize, 256 * 1024 * 1024));
    return mb;
}

juce::String zipEntryText (juce::ZipFile& zip, const juce::String& name)
{
    const int i = zip.getIndexOfFileName (name, true);
    if (i < 0) return {};
    const auto mb = zipEntryData (zip, i);
    return decodeText (mb.getData(), mb.getSize());
}

// ---------------------------------------------------------------- UltraStar
bool readUltraStar (const juce::MemoryBlock& data, ConvertedSong& out, juce::String& error)
{
    UltraStarSong us;
    if (! parseUltraStar (data, us)) { error = juce::String::fromUTF8 ("UltraStar-Datei ohne Noten oder Tempo"); return false; }
    out.format = "UltraStar";
    out.smf = ultraStarToSmf (us);
    out.title = us.title; out.artist = us.artist; out.genre = us.genre; out.comment = us.comment; out.year = us.year;
    out.lyrics = us.lyrics;
    out.language = us.language;
    out.writable = true;
    return true;
}

// ---------------------------------------------------------------- Reason (.reason / .rps / .rns)
// The sequencer data is encrypted; the song information block ("WWWI" records: KEY, "BODY", u32 size, u32 length, text)
// is readable: title, comment, tempo, metre, length and program version.
bool readReason (const juce::MemoryBlock& data, ConvertedSong& out, juce::String&)
{
    auto* d = static_cast<const char*> (data.getData());
    const size_t n = data.getSize();
    auto field = [&] (const char* key) -> juce::String {
        const auto kl = strlen (key);
        for (size_t i = 0; i + kl + 16 < n && i < 8 * 1024 * 1024; ++i)
        {
            if (memcmp (d + i, key, kl) != 0) continue;
            size_t p = i + kl;
            if (d[p] == 0) ++p;
            if (memcmp (d + p, "BODY", 4) != 0) continue;
            auto be = [&] (size_t at) { auto* u = reinterpret_cast<const juce::uint8*> (d + at); return (juce::uint32) ((u[0] << 24) | (u[1] << 16) | (u[2] << 8) | u[3]); };
            const auto len = be (p + 8);
            if (p + 12 + len > n || len > 4096) return {};
            return decodeText (d + p + 12, len).trim();
        }
        return {};
    };
    out.format = "Reason";
    out.title = field ("WINDOWTITLE");
    out.comment = field ("MORETEXT");
    if (out.comment.startsWithChar ('<') && out.comment.endsWithChar ('>')) out.comment = {};   // Reason placeholder "<Add text about song here>"
    const auto version = field ("PROGRAMVERSION");
    const double bpm = field ("SONGTEMPO").getDoubleValue() / 1000.0;
    const auto sig = field ("SONGSIGNATURE");
    const double ms = field ("SONGLENGTHMS").getDoubleValue();
    if (version.isNotEmpty()) out.comment = (out.comment.isNotEmpty() ? out.comment + " | " : juce::String()) + "Reason " + version;
    SmfBuilder b (480);
    b.tempo (0, bpm > 0 ? bpm : 120.0);
    if (sig.containsChar ('/')) b.timeSig (0, sig.upToFirstOccurrenceOf ("/", false, false).getIntValue(), sig.fromFirstOccurrenceOf ("/", false, false).getIntValue());
    if (ms > 0) b.endAt ((juce::int64) (ms / 1000.0 * (bpm > 0 ? bpm : 120.0) / 60.0 * 480.0));
    out.seconds = ms / 1000.0;
    b.text (0, 0, juce::String::fromUTF8 ("Reason-Song – Noten sind im Reason-Format verschlüsselt"));
    out.smf = b.build();
    return true;
}
} // namespace fmt

// ============================================================================================ dispatch
juce::String formatNameForFile (const juce::String& fileName)
{
    const auto e = ext (fileName);
    if (e == "txt") return "UltraStar";
    if (e == "xmi") return "XMI";
    if (e == "mus") return "MUS";
    if (e == "hmp") return "HMP";
    if (e == "cmf") return "CMF";
    if (e == "mmf") return "SMAF";
    if (e == "wrk") return "Cakewalk";
    if (e == "flp") return "FL Studio";
    if (e == "als") return "Ableton";
    if (e == "song") return "Studio One";
    if (e == "reason" || e == "rps" || e == "rns") return "Reason";
    if (e == "dawproject") return "DAWproject";
    if (e == "rpp") return "REAPER";
    if (e == "musicxml" || e == "mxl" || e == "xml") return "MusicXML";
    if (e == "mscz" || e == "mscx") return "MuseScore";
    if (e == "abc") return "ABC";
    if (e == "krn") return "Humdrum";
    return {};
}

bool hasLockedNotes (const juce::String& fileName) { return formatNameForFile (fileName) == "Reason"; }

juce::String lockedNotesMessage()
{
    return T ("Reason speichert die Noten verschlüsselt – nur Tempo, Taktart und Länge sind lesbar. In Reason: Datei → „MIDI-Datei exportieren …“ (bei .rps gesperrt).");
}

bool isConvertibleFile (const juce::File& f)
{
    const auto e = ext (f.getFileName());
    if (formatNameForFile (f.getFileName()).isEmpty()) return false;
    if (e == "txt") return isUltraStarFile (f);
    if (e == "xml") { const auto h = readHead (f, 4096); return looksLikeMusicXml (h); }
    if (e == "mus") return startsWith (readHead (f, 8), "MUS\x1a");
    if (e == "song" || e == "dawproject") return startsWith (readHead (f, 4), "PK");
    if (e == "rpp") return readHead (f, 64).toString().trimStart().startsWith ("<REAPER_PROJECT");
    return true;
}

bool convertToSmf (const juce::MemoryBlock& data, ConvertedSong& out, juce::String& error)
{
    using namespace fmt;
    out = {};
    const size_t n = data.getSize();
    if (n < 4) { error = T ("Datei nicht lesbar"); return false; }
    auto* d = static_cast<const juce::uint8*> (data.getData());

    if (looksLikeUltraStar (d, n)) return readUltraStar (data, out, error);
    if (startsWith (data, "FORM") && (startsWith (data, "XDIR", 8) || startsWith (data, "XMID", 8))) return readXmi (data, out, error);
    if (startsWith (data, "MUS\x1a")) return readMus (data, out, error);
    if (startsWith (data, "HMIMIDIP")) return readHmp (data, out, error);
    if (startsWith (data, "CTMF")) return readCmf (data, out, error);
    if (startsWith (data, "MMMD")) return readMmf (data, out, error);
    if (startsWith (data, "CAKEWALK")) return readWrk (data, out, error);
    if (startsWith (data, "FLhd")) return readFlp (data, out, error);
    if (startsWith (data, "X15_FILE") || startsWith (data, "Propellerheads Reason") || (startsWith (data, "FORM") && (startsWith (data, "RPS", 8) || startsWith (data, "RNPS", 8))))
        return readReason (data, out, error);
    if (d[0] == 0x1F && d[1] == 0x8B) return readAls (data, out, error);
    if (startsWith (data, "PK"))
    {
        juce::MemoryInputStream mis (data, false);
        juce::ZipFile zip (mis);
        if (zip.getIndexOfFileName ("Song/song.xml") >= 0) return readStudioOne (zip, out, error);
        if (zip.getIndexOfFileName ("project.xml") >= 0) return readDawProject (zip, out, error);
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto name = zip.getEntry (i)->filename;
            if (name.endsWithIgnoreCase (".mscx")) { const auto mb = zipEntryData (zip, i); return readMuseScore (decodeText (mb.getData(), mb.getSize()), out, error); }
        }
        if (const auto container = zipEntryText (zip, "META-INF/container.xml"); container.isNotEmpty())
            if (auto x = parseXml (container))
                if (auto* rf = x->getChildByName ("rootfiles"))
                    if (auto* r = rf->getChildByName ("rootfile"))
                        return readMusicXml (zipEntryText (zip, r->getStringAttribute ("full-path")), out, error);
        for (int i = 0; i < zip.getNumEntries(); ++i)
            if (zip.getEntry (i)->filename.endsWithIgnoreCase (".xml") || zip.getEntry (i)->filename.endsWithIgnoreCase (".musicxml"))
            {
                const auto mb = zipEntryData (zip, i);
                if (looksLikeMusicXml (mb)) return readMusicXml (decodeText (mb.getData(), mb.getSize()), out, error);
            }
        error = T ("Unbekanntes Archivformat");
        return false;
    }
    // text formats
    juce::MemoryBlock head (d, juce::jmin<size_t> (n, 8192));
    if (looksLikeMusicXml (head)) return readMusicXml (decodeText (d, n), out, error);
    if (looksLikeMuseScore (head)) return readMuseScore (decodeText (d, n), out, error);
    const auto text = decodeText (d, juce::jmin<size_t> (n, 16 * 1024 * 1024));
    if (text.trimStart().startsWith ("<REAPER_PROJECT")) return readReaper (text, out, error);
    if (text.contains ("**kern")) return readKern (text, out, error);
    if (looksLikeAbc (text)) return readAbc (text, out, error);
    error = juce::String::fromUTF8 ("Kein MIDI-Kopf (MThd)");
    return false;
}

juce::File midiCopyOf (const juce::File& src, const juce::File& folder)
{
    juce::MemoryBlock data;
    if (hasLockedNotes (src.getFileName()) || ! src.loadFileAsData (data)) return {};
    ConvertedSong s;
    juce::String err;
    if (! convertToSmf (data, s, err)) return {};
    auto name = s.title.isNotEmpty() ? (s.artist.isNotEmpty() ? s.artist + " - " : juce::String()) + s.title : src.getFileNameWithoutExtension();
    name = juce::File::createLegalFileName (name).substring (0, 120).trim();
    if (name.isEmpty()) name = "Midi-Fundus";
    folder.createDirectory();
    const auto out = folder.getChildFile (name + ".mid");
    return out.replaceWithData (s.smf.getData(), s.smf.getSize()) ? out : juce::File();
}
} // namespace ma
