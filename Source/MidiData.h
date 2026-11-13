// MIDIARCHIV – reading, analysing and rewriting Standard MIDI Files (SMF 0/1/2, RIFF RMID, KAR).
#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <vector>

namespace ma
{
constexpr int kSketch = 64;          // bottom-k sketch of melodic n-grams
constexpr int kMountBins = 32;       // time bins of the "mountain" profile
constexpr int kMountTracks = 6;      // stacked tracks in the profile (rest merged into the last)

struct Note
{
    float start = 0, len = 0;   // in quarter notes (beats)
    juce::uint8 pitch = 60, vel = 100, ch = 0, track = 0;
};

// Metadata fields that MIDIARCHIV can write back into the file.
enum class Field { title, artist, composer, genre, year, copyright, comment, language, keySignature };

struct MidiInfo
{
    bool ok = false;
    juce::String error;

    // file structure
    int format = 1, ppq = 96, numTracks = 0;
    bool rmi = false, karaoke = false;

    // metadata (as found in the file)
    juce::String title, artist, composer, genre, copyright, comment, language, lyrics;
    int year = 0;
    juce::StringArray trackNames, texts;

    // musical statistics
    double seconds = 0, bpm = 0;
    bool tempoChanges = false;
    juce::String timeSig, keyMeta, key, scale;
    float keyConfidence = 0;
    int noteCount = 0, channelCount = 0, drumNotes = 0;
    juce::uint16 channelMask = 0;
    std::array<bool, 128> programs {};
    juce::String instruments;   // short GM summary

    // fingerprints
    juce::uint64 noteHash = 0;
    std::vector<juce::uint32> sketch;   // sorted, up to kSketch values
    int gramCount = 0;
    std::array<juce::uint8, kMountBins * kMountTracks> mountain {};
    std::array<juce::uint8, 12> pitchClasses {};

    std::vector<Note> notes;    // only if requested
};

juce::uint32 crc32 (const void* data, size_t size);

// Parses a MIDI file. keepNotes = also return the note list (for drawing).
MidiInfo analyseMidi (const juce::MemoryBlock& data, bool keepNotes = false);
MidiInfo analyseMidiFile (const juce::File& f, bool keepNotes = false);

// Extracts the SMF bytes (unwraps RIFF RMID). Returns false if not a MIDI file.
bool extractSmf (const juce::MemoryBlock& data, juce::MemoryBlock& smf, size_t& smfOffset);

// Writes one metadata field into the file (track 0, tick 0). Atomic replace + one-time backup.
// Empty value removes the field. Returns false and fills error on failure.
bool writeMidiField (const juce::File& f, Field field, const juce::String& value, juce::String& error);
juce::File backupFolder();

// Musical helpers
juce::String gmProgramName (int program);
juce::String keyNameOf (int tonic, bool minor);                                   // "Fis-Dur", "a-Moll"
bool parseKeyName (const juce::String& text, int& tonic, bool& minor);           // "a-Moll", "Am", "fis", "Es-Dur", "C#m" …
juce::String noteName (int pitch);
juce::String parsonsCode (const std::vector<Note>& melody);           // *UDRU…
std::vector<Note> extractMelody (const std::vector<Note>& notes, int maxNotes = 24);
std::vector<juce::uint32> noteGrams (const std::vector<Note>& notes);   // sorted, unique
float sketchSimilarity (const std::vector<juce::uint32>& a, const std::vector<juce::uint32>& b);
juce::String detectLanguage (const juce::String& text);
} // namespace ma
