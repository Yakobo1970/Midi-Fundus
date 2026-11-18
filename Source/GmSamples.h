// MIDIARCHIV – the original waves of a General MIDI SoundFont preset (GeneralUser GS), for the Ableton Live Set export:
// every zone (key range, velocity range, root key, tuning, loop, envelope) of one preset, limited to the keys a track uses.
#pragma once
#include <juce_core/juce_core.h>
#include <set>
#include <vector>

namespace ma::gm
{
struct Wave
{
    std::vector<float> pcm;   // mono
    int rate = 44100;
    int root = 60;            // for the file name
    juce::uint32 id = 0;      // position in the SoundFont (same id = same wave)
};

struct Zone
{
    int wave = 0;                         // index into Preset::waves
    int loKey = 0, hiKey = 127, loVel = 0, hiVel = 127;
    int rootKey = 60;                     // Ableton: the key that plays the wave unchanged
    float detuneCents = 0, gainDb = 0, pan = 0;   // pan -1 .. 1
    int keyTrack = 100;                   // cents per key (SoundFont scaleTuning; Ableton TuneScale)
    int transpose = 0, tune = 0;          // raw SoundFont values (tests)
    bool loop = false;
    int loopStart = 0, loopEnd = 0;       // in the wave; loopEnd = first sample after the loop (as Ableton counts)
    float attack = 0, decay = 0, sustain = 1, release = 0.05f;   // seconds, sustain 0..1
};

struct Preset
{
    juce::String name;
    std::vector<Wave> waves;
    std::vector<Zone> zones;
};

// GeneralUser-GS.sf2 next to the program / plug-in, in %APPDATA%\MIDIARCHIV, $MIDIARCHIV_SF2 or Resources/SoundFont of the source tree
juce::File findSoundFont();

// bank 128 = drum kits (program = kit). keys: only zones that cover one of these keys (empty = all).
bool extract (const juce::File& sf2, int bank, int program, const std::set<int>& keys, Preset& out, juce::String& error);

// 16-bit mono PCM .wav
bool writeWav (const Wave& w, const juce::File& file);
} // namespace ma::gm
