#include "WebLookup.h"
#include "Lang.h"
#include <map>

namespace ma
{
namespace
{
const char* kUserAgent = "User-Agent: Midi-Fundus/0.1 (MIDI catalogue tool)";

juce::var getJson (const juce::String& url, juce::String& log)
{
    auto stream = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                         .withExtraHeaders (juce::String (kUserAgent) + "\r\nAccept: application/json")
                                                         .withConnectionTimeoutMs (12000));
    if (stream == nullptr) { log << T ("Keine Verbindung: %1", url.upToFirstOccurrenceOf ("?", false, false)) << "\n"; return {}; }
    auto text = stream->readEntireStreamAsString();
    auto v = juce::JSON::parse (text);
    if (v.isVoid() && text.containsIgnoreCase ("too many requests"))
        log << url.fromFirstOccurrenceOf ("//", false, false).upToFirstOccurrenceOf ("/", false, false)
            << T (": zu viele Anfragen in kurzer Zeit – bitte in ein paar Minuten erneut versuchen") << "\n";
    else if (v.isVoid()) log << T ("Antwort nicht lesbar: %1", url.upToFirstOccurrenceOf ("?", false, false)) << " - " << text.substring (0, 200).replaceCharacters ("\r\n", "  ") << "\n";
    return v;
}

int yearOf (const juce::String& date)
{
    // "1998-05-01", "+1786-01-01T00:00:00Z"
    auto s = date.trimCharactersAtStart ("+");
    const int y = s.substring (0, 4).getIntValue();
    return (y > 800 && y < 2100) ? y : 0;
}

juce::String normalise (juce::String s)
{
    s = s.toLowerCase();
    for (auto open : { "(", "[" })
        if (const int i = s.indexOf (open); i > 0) s = s.substring (0, i);
    return s.retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789äöüß").trim();
}

bool musicDescription (const juce::String& d)
{
    const auto t = d.toLowerCase();
    for (auto* w : { "song", "single", "lied", "composition", "komposition", "symphon", "sinfonie", "opera", "oper ", "sonat", "concerto", "konzert",
                     "musical work", "musikstück", "waltz", "walzer", "march", "marsch", "hymn", "aria", "arie", "carol", "anthem", "hymne", "overture",
                     "ouvertüre", "suite", "prelude", "präludium", "fugue", "fuge", "nocturne", "etude", "étude", "schlager", "volkslied", "folk", "track",
                     "musikalisches werk", "piece", "stück", "album", "requiem", "mass", "messe", "cantata", "kantate", "ballad", "ballade" })
        if (t.contains (juce::String::fromUTF8 (w))) return true;
    return false;
}
} // namespace

juce::String cleanTitleForSearch (juce::String t)
{
    t = t.replaceCharacter ('_', ' ');
    for (auto* junk : { ".mid", ".kar", "(midi)", "(karaoke)", "karaoke", "(remix)", "(instrumental)" })
        t = t.replace (junk, "", true);
    return t.trim();
}

juce::URL webSearchUrl (int engine, const juce::String& q)
{
    const auto e = juce::URL::addEscapeChars (q, true);
    switch (engine)
    {
        case 1: return juce::URL ("https://www.youtube.com/results?search_query=" + e);
        case 2: return juce::URL ("https://www.discogs.com/search/?q=" + e + "&type=all");
        case 3: return juce::URL ("https://musicbrainz.org/search?query=" + e + "&type=recording&method=indexed");
        case 4: return juce::URL ("https://" + langCode (language()) + ".wikipedia.org/w/index.php?search=" + e);
        case 5: return juce::URL ("https://imslp.org/index.php?title=Special:Search&search=" + e);
        case 6: return juce::URL ("https://www.allmusic.com/search/all/" + e);
        default: return juce::URL ("https://www.google.com/search?q=" + e);
    }
}

std::vector<Suggestion> lookupOnline (const LookupQuery& q, juce::String& log)
{
    std::vector<Suggestion> out;
    const auto title = cleanTitleForSearch (q.title);
    if (title.isEmpty()) { log << T ("Kein Titel zum Suchen") << "\n"; return out; }

    // ---------------------------------------------------------------- MusicBrainz
    {
        auto search = [&] (bool withArtist)
        {
            juce::String query = "recording:\"" + title.replace ("\"", "") + "\"";
            if (withArtist && q.artist.isNotEmpty()) query << " AND artist:\"" << q.artist.replace ("\"", "") << "\"";
            return getJson ("https://musicbrainz.org/ws/2/recording?fmt=json&limit=100&query=" + juce::URL::addEscapeChars (query, true), log);
        };
        auto v = search (true);
        auto recs = v["recordings"];
        if ((! recs.isArray() || recs.size() == 0) && q.artist.isNotEmpty()) { recs = search (false)["recordings"]; }
        std::map<juce::String, int> artists, tags;
        int minYear = 0, used = 0;
        const auto want = normalise (title);
        if (recs.isArray())
            for (auto& r : *recs.getArray())
            {
                const auto got = normalise (r["title"].toString());
                if ((int) r["score"] < 80) continue;
                if (got != want && ! (want.length() >= 6 && (got.contains (want) || want.contains (got) && got.length() >= 6))) continue;
                ++used;
                if (auto ac = r["artist-credit"]; ac.isArray() && ac.size() > 0)
                {
                    juce::String name;
                    for (auto& c : *ac.getArray()) name << c["name"].toString() << c["joinphrase"].toString();
                    if (name.isNotEmpty() && ! name.startsWith ("[")) ++artists[name.trim()];
                }
                auto consider = [&] (const juce::String& d) { const int y = yearOf (d); if (y > 0 && (minYear == 0 || y < minYear)) minYear = y; };
                consider (r["first-release-date"].toString());
                if (auto rel = r["releases"]; rel.isArray()) for (auto& x : *rel.getArray()) consider (x["date"].toString());
                if (auto tg = r["tags"]; tg.isArray()) for (auto& t : *tg.getArray()) tags[t["name"].toString()] += juce::jmax (1, (int) t["count"]);
            }
        log << T ("MusicBrainz: %1 passende Aufnahmen", used) << juce::newLine;
        auto best = [] (const std::map<juce::String, int>& m) { juce::String b; int c = 0; for (auto& [k, n] : m) if (n > c) { c = n; b = k; } return b; };
        if (auto a = best (artists); a.isNotEmpty()) out.push_back ({ Field::artist, a, "MusicBrainz (" + juce::String (artists[a]) + "x)" });
        if (minYear > 0) out.push_back ({ Field::year, juce::String (minYear), T ("MusicBrainz (früheste Veröffentlichung)") });
        if (auto g = best (tags); g.isNotEmpty()) out.push_back ({ Field::genre, g.substring (0, 1).toUpperCase() + g.substring (1), "MusicBrainz-Tags" });
    }

    // ---------------------------------------------------------------- Wikidata (works: composer, oldest date, genre)
    {
        juce::StringArray ids;
        juce::StringArray descs;
        juce::StringArray searchLangs { langCode (language()), "en", "de" };
        searchLangs.removeDuplicates (false);
        if (language() != Lang::de) searchLangs.removeString ("de");
        for (auto& lang : searchLangs)
        {
        auto s = getJson (juce::String ("https://www.wikidata.org/w/api.php?action=wbsearchentities&type=item&limit=10&format=json&language=") + lang
                          + "&uselang=" + lang + "&search=" + juce::URL::addEscapeChars (title, true), log);
        if (auto arr = s["search"]; arr.isArray())
            for (auto& x : *arr.getArray())
            {
                if (ids.contains (x["id"].toString())) continue;
                const auto label = x["label"].toString();
                const auto nl = normalise (label), nt = normalise (title);
                if (nl != nt && ! (nt.length() >= 6 && nl.contains (nt))) continue;
                if (! musicDescription (x["description"].toString())) continue;
                ids.add (x["id"].toString());
                descs.add (x["description"].toString());
            }
        }
        log << T ("Wikidata: %1 passende Werke", ids.size()) << "\n";
        if (! ids.isEmpty())
        {
            auto ent = getJson ("https://www.wikidata.org/w/api.php?action=wbgetentities&format=json&props=claims&ids=" + ids.joinIntoString ("|"), log);
            std::map<juce::String, int> composers, performers, genres;
            int minYear = 0;
            juce::String yearSource;
            if (auto* obj = ent["entities"].getDynamicObject())
                for (auto& [id, e] : obj->getProperties())
                {
                    auto claims = e["claims"];
                    auto itemIds = [&] (const char* prop, std::map<juce::String, int>& into)
                    {
                        if (auto c = claims[prop]; c.isArray())
                            for (auto& st : *c.getArray())
                                if (auto idv = st["mainsnak"]["datavalue"]["value"]["id"].toString(); idv.isNotEmpty()) ++into[idv];
                    };
                    itemIds ("P86", composers);
                    itemIds ("P175", performers);
                    itemIds ("P136", genres);
                    for (auto* prop : { "P571", "P577", "P1191" })
                        if (auto c = claims[prop]; c.isArray())
                            for (auto& st : *c.getArray())
                                if (const int y = yearOf (st["mainsnak"]["datavalue"]["value"]["time"].toString()); y > 0 && (minYear == 0 || y < minYear))
                                {
                                    minYear = y;
                                    yearSource = juce::String (prop) == "P571" ? T ("Wikidata (Entstehung)")
                                                                               : T ("Wikidata (Veröffentlichung)");
                                }
                }
            juce::StringArray need;
            for (auto* m : { &composers, &performers, &genres }) for (auto& [k, n] : *m) need.addIfNotAlreadyThere (k);
            std::map<juce::String, juce::String> labels;
            if (! need.isEmpty())
            {
                while (need.size() > 45) need.remove (need.size() - 1);
                auto lab = getJson ("https://www.wikidata.org/w/api.php?action=wbgetentities&format=json&props=labels&languages=de|en|fr&ids=" + need.joinIntoString ("|"), log);
                if (auto* obj = lab["entities"].getDynamicObject())
                    for (auto& [id, e] : obj->getProperties())
                    {
                        auto l = e["labels"][langCode (language()).toRawUTF8()]["value"].toString();
                        if (l.isEmpty()) l = e["labels"]["en"]["value"].toString();
                        if (l.isEmpty()) l = e["labels"]["de"]["value"].toString();
                        labels[id.toString()] = l;
                    }
            }
            auto best = [&] (const std::map<juce::String, int>& m) { juce::String b; int c = 0; for (auto& [k, n] : m) if (n > c && labels[k].isNotEmpty()) { c = n; b = labels[k]; } return b; };
            if (auto c = best (composers); c.isNotEmpty()) out.push_back ({ Field::composer, c, "Wikidata" });
            if (auto p = best (performers); p.isNotEmpty()) out.push_back ({ Field::artist, p, "Wikidata" });
            if (auto g = best (genres); g.isNotEmpty()) out.push_back ({ Field::genre, g, "Wikidata" });
            if (minYear > 0) out.push_back ({ Field::year, juce::String (minYear), yearSource });
        }
    }
    // oldest year first (it is pre-selected)
    std::stable_sort (out.begin(), out.end(), [] (const Suggestion& a, const Suggestion& b) {
        if (a.field != b.field) return (int) a.field < (int) b.field;
        if (a.field == Field::year) return a.value.getIntValue() < b.value.getIntValue();
        return false;
    });
    return out;
}
} // namespace ma
