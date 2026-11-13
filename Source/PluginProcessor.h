#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "SoundEngine.h"
#include "SoundFontPlayer.h"
#include <thread>

// Plays one MIDI file for previewing: built-in sound and/or MIDI out to the host.
class MidiArchivProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    MidiArchivProcessor();
    ~MidiArchivProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Midi-Fundus"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- preview player (message thread)
    bool play (const juce::File& f, double fromSeconds = 0.0);
    void stop();
    void seek (double seconds);
    bool isPlaying() const { return playing.load(); }
    double position() const { return pos.load(); }
    double length() const { return len.load(); }
    juce::String playingPath() const { const juce::ScopedLock sl (lock); return currentPath; }

    // preview sound: GM SoundFont (default), Windows "Microsoft GS Wavetable Synth", built-in Kreisel synth, or none (MIDI out only)
    enum SoundMode { sndSoundFont = 0, sndWindowsGm, sndKreisel, sndNone };
    void setSoundMode (int mode);          // message thread
    int soundMode() const { return mode.load(); }
    static bool windowsGmAvailable();
    void loadSoundFontAsync (const juce::File& sf2);   // empty = default GeneralUser GS
    SoundFontPlayer& soundFont() { return sfPlayer; }
    juce::String soundFontError() const { const juce::ScopedLock sl (lock); return sfError; }
    std::atomic<bool> midiOut { true };
    std::atomic<float> volume { 0.7f };

    juce::ValueTree uiState { "UI" };   // window size

    // ---- follow the DAW transport: host Play starts the cued (selected) file, host Stop stops it
    void setHostSync (bool on);          // saved globally (all projects)
    bool hostSync() const { return sync.load(); }
    void setHostTempo (bool on);         // DAW tempo drives the file (otherwise: DAW position, file's own tempo)
    bool hostTempo() const { return tempoSync.load(); }
    void setCue (const juce::String& path);
    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }
    std::function<void()> onHostTransport;   // editor: refresh status after a host start/stop

private:
    juce::CriticalSection lock;
    juce::MidiMessageSequence seq;
    juce::String currentPath;
    int nextEvent = 0;
    std::atomic<bool> playing { false }, flush { false };
    std::atomic<double> pos { 0.0 }, len { 0.0 };
    double sr = 48000.0;
    kr::SoundEngine synth;
    std::array<int, 16> program {};
    std::array<std::array<bool, 128>, 16> held {};
    juce::AudioBuffer<float> scratch;
    bool setWindowsGm (bool on);
    std::atomic<int> mode { sndSoundFont };
    SoundFontPlayer sfPlayer;
    juce::MidiBuffer sfBuf;
    std::atomic<bool> sfReset { false };
    std::thread sfThread;
    juce::String sfError, sfPath;
    std::unique_ptr<juce::MidiOutput> gmOut;
    juce::SpinLock gmLock;
    std::atomic<bool> gmOn { false };
    juce::MidiBuffer gmBuf;

    std::atomic<bool> sync { false }, tempoSync { true };
    std::atomic<int> hostEdge { 0 };     // audio thread -> timer: 1 = host started, -1 = host stopped
    std::atomic<bool> hostWasPlaying { false };
    std::atomic<bool> armed { false };   // seq holds the cued file (host Play can start it in the audio thread)
    juce::String cuePath;                // guarded by lock
    void timerCallback() override;
    static juce::File globalSettingsFile();
    void saveGlobalSettings();

    // beat timeline of the loaded file (quarter notes; empty for SMPTE files) – guarded by lock
    struct TempoPoint { double beat, sec, spb; };
    std::vector<double> beats;
    std::vector<TempoPoint> tempoMap;
    double expectPpq = -1.0;             // audio thread: where the host should be next block (detects jumps/loops)
    bool resync = false;                 // audio thread (under lock): place the file at the host position
    double beatToSec (double b) const;
    void placeAt (double beat, juce::MidiBuffer& out);   // audio thread, lock held
    bool load (const juce::File& f, bool start, double from);

    void handle (const juce::MidiMessage& m, juce::MidiBuffer& out, int sample);
    void allOff (juce::MidiBuffer& out);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiArchivProcessor)
};
