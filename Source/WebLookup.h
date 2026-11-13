// MIDIARCHIV – looks up missing song information online (MusicBrainz recordings, Wikidata works) and offers web searches.
#pragma once
#include <juce_core/juce_core.h>
#include "MidiData.h"

namespace ma
{
struct Suggestion
{
    Field field;
    juce::String value, source;
};

struct LookupQuery
{
    juce::String title, artist, composer;
};

// Blocking – call from a background thread.
std::vector<Suggestion> lookupOnline (const LookupQuery& q, juce::String& log);

juce::String cleanTitleForSearch (juce::String title);
juce::URL webSearchUrl (int engine, const juce::String& query);   // 0 Google, 1 YouTube, 2 Discogs, 3 MusicBrainz, 4 Wikipedia, 5 IMSLP, 6 AllMusic
inline const char* const kEngineNames[] = { "Google", "YouTube", "Discogs", "MusicBrainz", "Wikipedia", "IMSLP (Klassik-Noten)", "AllMusic" };
constexpr int kNumEngines = 7;
} // namespace ma
