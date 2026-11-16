// MIDIARCHIV – UltraStar karaoke files (.txt): detection, conversion to a Standard MIDI File, header editing.
// Format: header lines "#TAG:value" (TITLE, ARTIST, BPM, GAP, GENRE, YEAR, LANGUAGE, COMMENT, RELATIVE, ENCODING …),
// then note lines ": start length pitch syllable" (* golden, F freestyle, R/G rap), "- start" line breaks,
// "P1"/"P2" duet singers, "E" end. Time: ms = GAP + beat * 60000 / (BPM * 4); pitch 0 = C4 (MIDI 60).
#pragma once
#include "MidiData.h"

namespace ma
{
struct UltraStarSong
{
    juce::String title, artist, genre, language, comment, edition, creator;
    int year = 0;
    double bpm = 0, gapMs = 0;
    bool relative = false, utf8 = true, bom = false, crlf = true;

    struct Syllable { int start = 0, len = 0, pitch = 0, singer = 0; char type = ':'; juce::String text; };
    std::vector<Syllable> notes;
    std::vector<std::pair<int, int>> breaks;   // beat, singer
    juce::String lyrics;                        // singer 1, lines separated by "\n"
};

// Cheap check on the first bytes (header with #TITLE / #ARTIST / #BPM).
bool looksLikeUltraStar (const void* data, size_t size);
bool isUltraStarFile (const juce::File& f);

bool parseUltraStar (const juce::MemoryBlock& data, UltraStarSong& song);
juce::MemoryBlock ultraStarToSmf (const UltraStarSong& song);

// Rewrites one header line. Keeps encoding, BOM and line endings. Fields without an UltraStar tag fail.
bool writeUltraStarField (const juce::MemoryBlock& data, Field field, const juce::String& value,
                          juce::MemoryBlock& result, juce::String& error);

// Writes the converted MIDI file (for drag & drop into a DAW or "save as MIDI").
juce::File ultraStarMidiCopy (const juce::File& txt, const juce::File& folder);
} // namespace ma
