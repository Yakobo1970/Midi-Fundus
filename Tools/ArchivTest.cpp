// MIDIARCHIV console tests.
//   ArchivTest info <file>                 – analysis of one file
//   ArchivTest write <file>                – writes all fields into a temp copy, checks read-back and unchanged notes
//   ArchivTest scan <db> <folder> [...]    – scans folders into <db>, prints timing and duplicate groups
//   ArchivTest groups <db> [n]             – prints the first n duplicate groups of an existing db
#include "../Source/Columns.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Source/WebLookup.h"
#include <iostream>
#include <map>

static void out (const juce::String& s) { std::cout << s.toRawUTF8() << std::endl; }
static int failures = 0;
static void check (bool ok, const juce::String& what) { out ((ok ? "ok:   " : "FAIL: ") + what); if (! ok) ++failures; }

static void printInfo (const ma::MidiInfo& m)
{
    out ("ok=" + juce::String ((int) m.ok) + " err=" + m.error + " format=" + juce::String (m.format) + " tracks=" + juce::String (m.numTracks) + " ppq=" + juce::String (m.ppq));
    out ("title=" + m.title + " | artist=" + m.artist + " | composer=" + m.composer + " | genre=" + m.genre + " | year=" + juce::String (m.year));
    out ("copyright=" + m.copyright + " | language=" + m.language + " | karaoke=" + juce::String ((int) m.karaoke) + " lyrics=" + juce::String (m.lyrics.length()));
    out ("comment=" + m.comment.substring (0, 200));
    out ("key=" + m.key + " (" + juce::String (m.keyConfidence, 2) + ") meta=" + m.keyMeta + " scale=" + m.scale + " bpm=" + juce::String (m.bpm, 1)
         + " sig=" + m.timeSig + " dur=" + ma::formatDuration (m.seconds));
    out ("notes=" + juce::String (m.noteCount) + " drums=" + juce::String (m.drumNotes) + " channels=" + juce::String (m.channelCount) + " instr=" + m.instruments);
    out ("grams=" + juce::String (m.gramCount) + " sketch=" + juce::String ((int) m.sketch.size()) + " noteHash=" + juce::String::toHexString ((juce::int64) m.noteHash));
    out ("tracks: " + m.trackNames.joinIntoString (" / "));
}

static int writeTest (const juce::File& src)
{
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ma_writetest" + src.getFileExtension());
    src.copyFileTo (tmp);
    const auto before = ma::analyseMidiFile (tmp);
    check (before.ok, "parse original");
    struct T { ma::Field f; juce::String v; };
    const T tests[] = { { ma::Field::title, juce::String::fromUTF8 ("Testtitel Ä") }, { ma::Field::artist, "Test Artist" }, { ma::Field::composer, "J. S. Bach" },
                        { ma::Field::genre, "Klassik" }, { ma::Field::year, "1723" }, { ma::Field::copyright, "(C) Test" },
                        { ma::Field::comment, "Kommentar 1" }, { ma::Field::language, "Deutsch" }, { ma::Field::keySignature, "fis-Moll" } };
    for (auto& t : tests)
    {
        juce::String err;
        const bool ok = ma::writeMidiField (tmp, t.f, t.v, err);
        check (ok, "write field " + juce::String ((int) t.f) + " " + err);
    }
    auto a = ma::analyseMidiFile (tmp);
    check (a.title == tests[0].v, "title read back: " + a.title);
    check (a.artist == "Test Artist", "artist read back: " + a.artist);
    check (a.composer == "J. S. Bach", "composer read back: " + a.composer);
    check (a.genre == "Klassik", "genre read back");
    check (a.year == 1723, "year read back");
    check (a.copyright == "(C) Test", "copyright read back");
    check (a.comment == "Kommentar 1", "comment read back");
    check (a.language == "Deutsch", "language read back");
    check (a.keyMeta == "fis-Moll", "key signature read back: " + a.keyMeta);
    {
        int t; bool mi;
        check (ma::parseKeyName ("Es-Dur", t, mi) && t == 3 && ! mi, "parse Es-Dur");
        check (ma::parseKeyName ("F#m", t, mi) && t == 6 && mi, "parse F#m");
        check (ma::parseKeyName ("b", t, mi) && t == 10 && mi, "parse b = b-Moll");
        check (ma::parseKeyName ("H", t, mi) && t == 11 && ! mi, "parse H");
        check (ma::parseKeyName ("as-moll", t, mi) && t == 8 && mi, "parse as-moll");
        check (! ma::parseKeyName ("xyz", t, mi), "reject xyz");
    }
    check (a.noteHash == before.noteHash && a.noteCount == before.noteCount, "notes unchanged");
    // overwrite and remove
    juce::String err;
    ma::writeMidiField (tmp, ma::Field::artist, "Second", err);
    ma::writeMidiField (tmp, ma::Field::genre, "", err);
    auto b = ma::analyseMidiFile (tmp);
    check (b.artist == "Second", "artist overwritten (no duplicate tag)");
    check (b.genre.isEmpty(), "genre removed");
    // JUCE reads it too
    {
        juce::MemoryBlock mb, smf; size_t off = 0;
        tmp.loadFileAsData (mb);
        ma::extractSmf (mb, smf, off);
        juce::MemoryInputStream is (smf, false);
        juce::MidiFile mf;
        check (mf.readFrom (is), "juce::MidiFile reads the rewritten file");
    }
    tmp.deleteFile();
    return failures;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    _putenv_s ("MIDIARCHIV_BACKUP", juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ma_test_backup").getFullPathName().toRawUTF8());
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (juce::String::fromUTF8 (argv[i]));
    if (args.size() >= 2 && args[0] == "info") { printInfo (ma::analyseMidiFile (juce::File (args[1]))); return 0; }
    if (args.size() >= 2 && args[0] == "lookup")
    {
        juce::String log;
        auto res = ma::lookupOnline ({ args[1], args.size() > 2 ? args[2] : juce::String(), {} }, log);
        out (log);
        for (auto& r : res) out (juce::String ((int) r.field) + ": " + r.value + "   <- " + r.source);
        return 0;
    }
    if (args.size() >= 2 && args[0] == "fileops")
    {
        auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ma_fileops");
        root.deleteRecursively();
        auto a = root.getChildFile ("A"), b = root.getChildFile ("B");
        a.createDirectory(); b.createDirectory();
        juce::File (args[1]).copyFileTo (a.getChildFile ("one.mid"));
        juce::File (args[1]).copyFileTo (a.getChildFile ("two.mid"));
        _putenv_s ("MIDIARCHIV_DB", root.getChildFile ("t.mdb").getFullPathName().toRawUTF8());
        ma::Archive arch;
        arch.waitForIndex();
        arch.addFolder (a);
        arch.startScan();
        arch.waitForScan();
        check (arch.entries().size() == 2, "scan finds 2");
        check (arch.duplicateGroups() == 1, "identical copies form one group");
        auto* one = arch.byPath (a.getChildFile ("one.mid").getFullPathName());
        one->rating = 4;
        const int id = one->id;
        // move to B
        auto dst = b.getChildFile ("one.mid");
        check (a.getChildFile ("one.mid").moveFileTo (dst), "move file");
        arch.relocate (*one, dst);
        check (arch.byPath (dst.getFullPathName()) == one && one->id == id && one->rating == 4, "relocate keeps id and rating");
        check (arch.byPath (a.getChildFile ("one.mid").getFullPathName()) == nullptr, "old path gone");
        // copy into archive folder
        auto copy = a.getChildFile ("three.mid");
        dst.copyFileTo (copy);
        check (arch.addFile (copy) != nullptr && arch.entries().size() == 3, "addFile adds a copy");
        check (arch.byPath (copy.getFullPathName())->groupSize == 3, "copy joins the duplicate group");
        // delete
        arch.removeEntries ({ id });
        check (arch.entries().size() == 2 && arch.byId (id) == nullptr, "removeEntries");
        check (arch.byPath (copy.getFullPathName())->groupSize == 2, "group shrinks after removal");
        check (arch.isInsideFolders (copy) && ! arch.isInsideFolders (dst), "isInsideFolders");
        root.deleteRecursively();
        return failures;
    }
    if (args.size() >= 2 && args[0] == "write") return writeTest (juce::File (args[1]));
    if (args.size() >= 1 && args[0] == "lang")
    {
        int failures = 0;
        auto check = [&] (ma::Lang l, const char* input, const char* expectGerman)
        {
            ma::setLanguage (l);
            const auto got = ma::keyInputToGerman (juce::String::fromUTF8 (input));
            const bool ok = got == juce::String::fromUTF8 (expectGerman);
            if (! ok) { out ("FEHLER " + ma::langCode (l) + ": " + juce::String::fromUTF8 (input) + " -> " + got + " (erwartet " + juce::String::fromUTF8 (expectGerman) + ")"); ++failures; }
        };
        using ma::Lang;
        check (Lang::en, "F# minor", "fis-Moll");   check (Lang::en, "Bb major", "B-Dur");     check (Lang::en, "B major", "H-Dur");
        check (Lang::en, "Am", "a-Moll");           check (Lang::en, "Eb", "Es-Dur");          check (Lang::en, "c sharp minor", "cis-Moll");
        check (Lang::en, "fis-Moll", "fis-Moll");   check (Lang::en, "Es-Dur", "Es-Dur");      check (Lang::en, "h-Moll", "h-Moll");
        check (Lang::fr, "fa# mineur", "fis-Moll"); check (Lang::fr, "si bémol majeur", "B-Dur"); check (Lang::fr, "ré mineur", "d-Moll");
        check (Lang::fr, "mib", "Es-Dur");          check (Lang::fr, "sol", "G-Dur");          check (Lang::fr, "la m", "a-Moll");
        check (Lang::fr, "B major", "H-Dur");       check (Lang::de, "B-Dur", "B-Dur");        check (Lang::de, "Am", "Am");
        // display round trip: every key name -> local -> back
        for (int l = 1; l <= 2; ++l)
            for (int t = 0; t < 12; ++t)
                for (bool m : { false, true })
                {
                    ma::setLanguage ((Lang) l);
                    const auto de = ma::keyNameOf (t, m);
                    const auto shown = ma::localKey (de);
                    const auto back = ma::keyInputToGerman (shown);
                    if (back != de) { out ("FEHLER Rundlauf " + ma::langCode ((Lang) l) + ": " + de + " -> " + shown + " -> " + back); ++failures; }
                }
        ma::setLanguage (Lang::en);
        out ("en: " + ma::localKey (juce::String::fromUTF8 ("fis-Moll?")) + " | " + ma::localMode ("fis-Moll?") + " | " + ma::localScale (juce::String::fromUTF8 ("Harmonisch Moll · 7"))
             + " | " + ma::localLanguageName (juce::String::fromUTF8 ("Französisch")) + " | " + ma::T ("%1 Dateien  ·  %2 angezeigt  ·  %3 Dubletten-Gruppen", 3, 2, 1));
        ma::setLanguage (Lang::fr);
        out (juce::String::fromUTF8 ("fr: ") + ma::localKey ("B-Dur") + " | " + ma::localMode ("a-Moll") + " | " + ma::localScale (juce::String::fromUTF8 ("Erweitert · 8"))
             + " | " + ma::localLanguageName ("Englisch") + " | " + ma::TS ("Datei nicht lesbar"));
        out (failures == 0 ? "lang ALL OK" : "lang FAILURES " + juce::String (failures));
        return failures;
    }
    if (args.size() >= 2 && args[0] == "notdup")
    {
        juce::File db (args[1]);
        _putenv_s ("MIDIARCHIV_DB", db.getFullPathName().toRawUTF8());
        int failures = 0;
        std::vector<std::pair<int, int>> done;
        {
            ma::Archive arch;
            arch.waitForIndex();
            // pick pairs: indirect (group only) and direct links, in groups of size >= 4
            std::map<int, std::vector<int>> groups;
            for (auto& e : arch.entries()) if (e->group) groups[e->group].push_back (e->id);
            int indirect = 0, direct = 0;
            for (auto& [g, v] : groups)
            {
                if (v.size() < 4 || (indirect >= 3 && direct >= 3)) continue;
                const int a = v[0];
                auto la = arch.linksOf (a);
                for (int b : v)
                {
                    if (b == a) continue;
                    const bool isDirect = std::any_of (la.begin(), la.end(), [b] (auto& l) { return l.other == b; });
                    if (isDirect ? direct >= 3 : indirect >= 3) continue;
                    const int before = arch.byId (a)->groupSize;
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    arch.markNotDuplicate (a, b);
                    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                    auto* ea = arch.byId (a); auto* eb = arch.byId (b);
                    const bool ok = ea->group == 0 || ea->group != eb->group;
                    out (juce::String (isDirect ? "direct  " : "indirect") + " size " + juce::String (before) + " -> A " + juce::String (ea->groupSize)
                         + " / B " + juce::String (eb->groupSize) + "  " + juce::String (ms, 1) + " ms  " + (ok ? "ok" : "FEHLER"));
                    failures += ok ? 0 : 1;
                    done.push_back ({ a, b });
                    (isDirect ? direct : indirect)++;
                    break;
                }
            }
            arch.save();
        }
        {
            ma::Archive arch;       // reload + full rebuild must keep the separation
            arch.waitForIndex();
            arch.rebuildDuplicates();
            for (auto [a, b] : done)
            {
                auto* ea = arch.byId (a); auto* eb = arch.byId (b);
                const bool ok = ea->group == 0 || ea->group != eb->group;
                if (! ok) { out ("nach Neuaufbau wieder verbunden: " + ea->name + " / " + eb->name); ++failures; }
            }
        }
        out (failures == 0 ? "notdup ALL OK" : "notdup FAILURES " + juce::String (failures));
        return failures;
    }
    if (args.size() >= 2 && (args[0] == "scan" || args[0] == "groups"))
    {
        juce::File db (args[1]);
        if (args[0] == "scan") db.deleteFile();
        _putenv_s ("MIDIARCHIV_DB", db.getFullPathName().toRawUTF8());
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        ma::Archive arch;
        out ("loaded " + juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 2) + "s, groups from db=" + juce::String (arch.duplicateGroups()));
        arch.waitForIndex();
        out ("index ready " + juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 2) + "s");
        if (args[0] == "scan")
        {
            for (int i = 2; i < args.size(); ++i) arch.addFolder (juce::File (args[i]));
            arch.startScan();
            arch.waitForScan();
        }
        arch.waitForIndex();
        arch.save();
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        int errors = 0, kar = 0, fromName = 0;
        for (auto& e : arch.entries()) { errors += e->has (ma::fError); kar += e->lyrics.isNotEmpty(); fromName += e->has (ma::fTitleFromName); }
        out ("entries=" + juce::String ((int) arch.entries().size()) + " errors=" + juce::String (errors) + " lyrics=" + juce::String (kar)
             + " titleFromName=" + juce::String (fromName) + " groups=" + juce::String (arch.duplicateGroups()) + " time=" + juce::String ((t1 - t0) / 1000.0, 1) + "s");
        const auto t2 = juce::Time::getMillisecondCounterHiRes();
        arch.rebuildDuplicates();
        out ("rebuildDuplicates " + juce::String ((juce::Time::getMillisecondCounterHiRes() - t2) / 1000.0, 2) + "s");
        std::map<int, std::vector<ma::Entry*>> groups;
        for (auto& e : arch.entries()) if (e->group) groups[e->group].push_back (e.get());
        int shown = 0;
        const int limit = args.size() > 2 && args[0] == "groups" ? args[2].getIntValue() : 25;
        int kinds[4] {};
        for (auto& e : arch.entries()) ++kinds[e->dupKind];
        out ("kinds: identical=" + juce::String (kinds[3]) + " sameNotes=" + juce::String (kinds[2]) + " similar=" + juce::String (kinds[1]));
        for (auto& [g, v] : groups)
        {
            if (shown++ >= limit) break;
            out ("--- group " + juce::String (g) + " (" + juce::String ((int) v.size()) + ")");
            for (auto* e : v)
                out ("   " + ma::dupText (*e) + "  " + e->folder.fromLastOccurrenceOf ("\\", false, false) + "\\" + e->name + "  [" + e->title + " / " + e->artist + "]");
        }
        return 0;
    }
    out ("usage: ArchivTest info|write <file> | scan <db> <folder...> | groups <db> [n]");
    return 1;
}
