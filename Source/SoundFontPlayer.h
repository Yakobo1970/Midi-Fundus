// MIDIARCHIV – General MIDI playback from a SoundFont (.sf2) via TinySoundFont (MIT, Bernhard Schelling).
// Default bank: GeneralUser GS by S. Christian Collins (free to use and redistribute, see LICENSE_GeneralUser.txt).
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <array>
#include <atomic>

struct tsf;

class SoundFontPlayer
{
public:
    ~SoundFontPlayer();

    bool load (const juce::File& sf2, juce::String& error);   // any thread except the audio thread
    bool isLoaded() const { return loaded.load(); }
    juce::File file() const { return current; }

    void prepare (double sampleRate, int maxBlock);
    // audio thread: plays the events (sample positions) and adds the sound into left/right
    void process (const juce::MidiBuffer& events, float* left, float* right, int numSamples, float gain);
    void reset (const std::array<int, 16>& programs);   // audio thread: everything off, programs restored

    int activeVoices() const;                            // diagnostics (tests)
    static juce::File defaultSoundFont();                 // GeneralUser-GS.sf2 next to the plug-in / exe or in %APPDATA%\MIDIARCHIV

private:
    tsf* sf = nullptr;
    juce::SpinLock lock;
    std::atomic<bool> loaded { false };
    juce::File current;
    double sr = 48000.0;
    std::vector<float> buffer;
    void handle (const juce::MidiMessage& m);
    void renderInto (float* l, float* r, int n, float gain);
};
