// MIDIARCHIV – table columns: names, widths, cell texts, sort keys and the search-mask filter syntax.
#pragma once
#include "Archive.h"
#include "Lang.h"
#include "Formats.h"

namespace ma
{
enum Col
{
    cId = 1, cFile, cTitle, cArtist, cComposer, cGenre, cYear, cCopyright, cLanguage, cKey, cScale, cTempo, cTimeSig,
    cDuration, cTracks, cChannels, cNotes, cInstruments, cKaraoke, cRating, cFav, cProfile, cDup, cComment, cSize,
    cFormat, cKeyMeta, cCrc, cFolder, cModified, cMode, kColEnd
};

struct ColDef
{
    int id;
    const char* name;
    int width;
    bool numeric;
    int field;        // Field written into the file, -1 = none
    const char* tip;
};

inline const ColDef* columnDefs()
{
    static const ColDef defs[] = {
        { cId, "Nr", 56, true, -1, "Laufende Nummer in der Datenbank" },
        { cFile, "Dateiname", 220, false, -1, "Name der MIDI-Datei" },
        { cTitle, "Titel", 200, false, (int) Field::title, "Songtitel – Klick zum Bearbeiten, wird in die Datei geschrieben. Kursiv = aus dem Dateinamen abgeleitet" },
        { cArtist, "Interpret", 160, false, (int) Field::artist, "Interpret – Klick zum Bearbeiten. Kursiv = aus dem Dateinamen abgeleitet" },
        { cComposer, "Komponist", 140, false, (int) Field::composer, "Komponist – Klick zum Bearbeiten" },
        { cGenre, "Genre", 100, false, (int) Field::genre, "Genre – Klick zum Bearbeiten" },
        { cYear, "Jahr", 56, true, (int) Field::year, "Erscheinungsjahr (bei Klassik: Entstehung) – Klick zum Bearbeiten" },
        { cCopyright, "Copyright", 180, false, (int) Field::copyright, "Copyright-Eintrag der Datei – Klick zum Bearbeiten" },
        { cLanguage, "Sprache", 80, false, (int) Field::language, "Sprache der Liedtexte (aus Karaoke-Kopf oder erkannt)" },
        { cKey, "Tonart", 78, false, -1, "Aus den Noten ermittelte Tonart (? = unsicher)" },
        { cMode, "Tongeschlecht", 92, false, -1, "Dur oder Moll – aus den Noten ermittelt (? = unsicher)" },
        { cScale, "Skala", 120, false, -1, "Tonvorrat: Diatonisch, Pentatonik, Harmonisch Moll, Blues, Chromatisch … und Anzahl genutzter Töne von 12" },
        { cTempo, "Tempo", 58, true, -1, "Anfangstempo in BPM (* = Tempowechsel)" },
        { cTimeSig, "Takt", 46, false, -1, "Taktart" },
        { cDuration, "Dauer", 54, true, -1, "Spieldauer" },
        { cTracks, "Spuren", 52, true, -1, "Anzahl der Spuren" },
        { cChannels, "Kanäle", 54, true, -1, "Anzahl benutzter MIDI-Kanäle" },
        { cNotes, "Noten", 60, true, -1, "Anzahl der Noten" },
        { cInstruments, "Instrumente", 200, false, -1, "General-MIDI-Instrumente (Program Change)" },
        { cKaraoke, "Kar.Text", 64, true, -1, "Liedtext vorhanden (Anzahl Zeichen)" },
        { cRating, "Bewertung", 72, true, -1, "0–5 Sterne – Klick auf einen Stern (nur in der Datenbank)" },
        { cFav, "Favorit", 52, true, -1, "Favorit – Klick schaltet um (nur in der Datenbank)" },
        { cProfile, "Profil", 110, true, -1, "Notengebirge: Notendichte über die Zeit, je Spur eine Farbe gestapelt" },
        { cDup, "Dublette", 120, true, -1, "Dubletten-Gruppe: identisch, gleiche Noten oder ähnlich (%)" },
        { cComment, "Kommentar", 240, false, (int) Field::comment, "Kommentar bzw. Texte der Datei – Klick zum Bearbeiten" },
        { cSize, "Größe", 66, true, -1, "Dateigröße in Byte" },
        { cFormat, "Format", 52, true, -1, "SMF-Format 0/1/2 (RMI = RIFF-MIDI)" },
        { cKeyMeta, "Tonart (Datei)", 100, false, (int) Field::keySignature, "In der Datei eingetragene Tonart (Key Signature) – Klick zum Bearbeiten, z. B. a-Moll, Es-Dur, F#m. Rechtsklick: ermittelte Tonart eintragen" },
        { cCrc, "CRC32", 74, false, -1, "Prüfsumme der Datei" },
        { cFolder, "Ordner", 300, false, -1, "Ordner der Datei" },
        { cModified, "Geändert", 120, true, -1, "Letzte Änderung der Datei" },
        { 0, nullptr, 0, false, -1, nullptr } };
    return defs;
}

inline const ColDef& colDef (int id)
{
    for (auto* d = columnDefs(); d->id != 0; ++d) if (d->id == id) return *d;
    return columnDefs()[0];
}

inline juce::String colName (int id) { return T (colDef (id).name); }
inline juce::String colTip (int id) { return T (colDef (id).tip); }

inline juce::String dupText (const Entry& e)
{
    if (e.group == 0) return {};
    juce::String k = e.dupKind == dupIdentical ? T ("identisch")
                   : e.dupKind == dupSameNotes ? T ("gleiche Noten")
                                               : T ("ähnlich %1 %", juce::roundToInt (e.bestScore * 100));
    return k + " (" + juce::String (e.groupSize) + ")";
}

inline juce::String cellText (const Entry& e, int col)
{
    switch (col)
    {
        case cId: return juce::String (e.id).paddedLeft ('0', 5);
        case cFile: return e.name;
        case cTitle: return e.title;
        case cArtist: return e.artist;
        case cComposer: return e.composer;
        case cGenre: return e.genre;
        case cYear: return e.year > 0 ? juce::String (e.year) : juce::String();
        case cCopyright: return e.copyright;
        case cLanguage: return localLanguageName (e.language);
        case cKey: return localKey (e.key);
        case cScale: return localScale (e.scale);
        case cMode: return localMode (e.key);
        case cTempo: return e.bpm > 0 ? juce::String (e.bpm, e.bpm == std::floor (e.bpm) ? 0 : 1) + (e.has (fTempoChanges) ? "*" : "") : juce::String();
        case cTimeSig: return e.timeSig;
        case cDuration: return e.has (fError) ? juce::String() : formatDuration (e.seconds);
        case cTracks: return juce::String (e.tracks);
        case cChannels: return juce::String (e.channels);
        case cNotes: return juce::String (e.notes);
        case cInstruments: return e.instruments;
        case cKaraoke: return e.lyrics.isNotEmpty() ? juce::String (e.lyrics.length()) : juce::String();
        case cRating: return e.rating > 0 ? juce::String (e.rating) : juce::String();
        case cFav: return e.favourite ? T ("ja") : juce::String();
        case cProfile: return {};
        case cDup: return dupText (e);
        case cComment: return e.has (fError) ? T ("FEHLER: %1", TS (e.error)) : e.comment;
        case cSize: return juce::String (e.size);
        case cFormat: return e.has (fConverted) ? formatNameForFile (e.name) : e.has (fRmi) ? "RMI" : juce::String (e.format);
        case cKeyMeta: return localKey (e.keyMeta);
        case cCrc: return juce::String::toHexString ((int) e.crc).paddedLeft ('0', 8).toUpperCase();
        case cFolder: return e.folder;
        case cModified: return localDate (e.mtime);
        default: return {};
    }
}

inline double cellNumber (const Entry& e, int col)
{
    switch (col)
    {
        case cId: return e.id;
        case cYear: return e.year;
        case cTempo: return e.bpm;
        case cDuration: return e.seconds;
        case cTracks: return e.tracks;
        case cChannels: return e.channels;
        case cNotes: return e.notes;
        case cKaraoke: return e.lyrics.length();
        case cRating: return e.rating;
        case cFav: return e.favourite ? 1 : 0;
        case cProfile: return e.notes;
        case cDup: return e.group == 0 ? 1e12 : (double) e.group * 10.0 - e.dupKind - e.bestScore;
        case cSize: return (double) e.size;
        case cFormat: return e.has (fConverted) ? 10 + (int) (formatNameForFile (e.name).hashCode() & 0xFF) : e.has (fRmi) ? 9 : e.format;
        case cModified: return (double) e.mtime;
        default: return 0;
    }
}

// ---- search mask
// Text: all words must occur (case-insensitive). "!wort" = enthält nicht, "=" = leer, "!" = nicht leer, "=text" = genau.
// Numbers: ">120", "<=3:00", "100-130", "=1998". Duration accepts m:ss.
inline double parseNumber (juce::String s, bool& ok)
{
    s = s.trim().replaceCharacter (',', '.');
    ok = s.isNotEmpty() && s.containsOnly ("0123456789.:-");
    if (! ok) return 0;
    if (s.contains (":")) return s.upToFirstOccurrenceOf (":", false, false).getIntValue() * 60.0 + s.fromFirstOccurrenceOf (":", false, false).getDoubleValue();
    return s.getDoubleValue();
}

inline bool matchFilter (const Entry& e, int col, const juce::String& rawFilter)
{
    const auto f = rawFilter.trim();
    if (f.isEmpty()) return true;
    const auto text = cellText (e, col);
    if (f == "=") return text.isEmpty() && (col != cDup || e.group == 0);
    if (f == "!") return text.isNotEmpty();
    if (colDef (col).numeric && col != cDup && col != cProfile)
    {
        const double v = cellNumber (e, col);
        bool ok = false;
        auto op = f.substring (0, 2);
        if (op == ">=" || op == "<=") { const double x = parseNumber (f.substring (2), ok); if (ok) return op == ">=" ? v >= x : v <= x; }
        if (f[0] == '>' || f[0] == '<') { const double x = parseNumber (f.substring (1), ok); if (ok) return f[0] == '>' ? v > x : v < x; }
        if (f[0] == '=') { const double x = parseNumber (f.substring (1), ok); if (ok) return std::abs (v - x) < 0.5; }
        const int dash = f.indexOfChar (1, '-');
        if (dash > 0)
        {
            bool ok2 = false;
            const double a = parseNumber (f.substring (0, dash), ok), b = parseNumber (f.substring (dash + 1), ok2);
            if (ok && ok2) return v >= a && v <= b;
        }
    }
    if (f[0] == '=' ) return text.equalsIgnoreCase (f.substring (1).trim());
    juce::StringArray words;
    words.addTokens (f, " ", "\"");
    words.removeEmptyStrings();
    for (auto w : words)
    {
        w = w.unquoted();
        if (w.startsWithChar ('!') && w.length() > 1) { if (text.containsIgnoreCase (w.substring (1))) return false; }
        else if (! text.containsIgnoreCase (w)) return false;
    }
    return true;
}
} // namespace ma
