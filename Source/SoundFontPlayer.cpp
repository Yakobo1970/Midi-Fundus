#include "SoundFontPlayer.h"
#include "Lang.h"
#include <array>

#if defined (_MSC_VER)
 #pragma warning (push, 0)
#endif
#define TSF_IMPLEMENTATION
#include "ThirdParty/tsf.h"
#if defined (_MSC_VER)
 #pragma warning (pop)
#endif

SoundFontPlayer::~SoundFontPlayer()
{
    if (sf != nullptr) tsf_close (sf);
}

juce::File SoundFontPlayer::defaultSoundFont()
{
    const auto module = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    for (auto f : { module.getChildFile ("GeneralUser-GS.sf2"),
                    module.getChildFile ("SoundFont").getChildFile ("GeneralUser-GS.sf2"),
                    module.getParentDirectory().getChildFile ("Resources").getChildFile ("GeneralUser-GS.sf2"),   // inside MIDIARCHIV.vst3
                    juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MIDIARCHIV").getChildFile ("GeneralUser-GS.sf2") })
        if (f.existsAsFile()) return f;
    return {};
}

bool SoundFontPlayer::load (const juce::File& file, juce::String& error)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data)) { error = ma::T ("SoundFont nicht lesbar: ") + file.getFullPathName(); return false; }
    tsf* fresh = tsf_load_memory (data.getData(), (int) data.getSize());
    if (fresh == nullptr) { error = ma::T ("Keine gültige SoundFont-Datei: ") + file.getFileName(); return false; }
    tsf_set_max_voices (fresh, 256);   // pre-allocate: no allocation in the audio thread
    tsf_set_output (fresh, TSF_STEREO_INTERLEAVED, (int) sr, 0.0f);
    for (int ch = 0; ch < 16; ++ch) tsf_channel_set_presetnumber (fresh, ch, 0, ch == 9);
    tsf* old = nullptr;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = sf;
        sf = fresh;
        current = file;
        loaded = true;
    }
    if (old != nullptr) tsf_close (old);
    return true;
}

void SoundFontPlayer::prepare (double sampleRate, int maxBlock)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    sr = sampleRate;
    buffer.assign ((size_t) juce::jmax (512, maxBlock) * 2, 0.0f);
    if (sf != nullptr) tsf_set_output (sf, TSF_STEREO_INTERLEAVED, (int) sr, 0.0f);
}

void SoundFontPlayer::handle (const juce::MidiMessage& m)
{
    const int ch = m.getChannel() - 1;
    if (ch < 0) return;
    if (m.isNoteOn()) tsf_channel_note_on (sf, ch, m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff()) tsf_channel_note_off (sf, ch, m.getNoteNumber());
    else if (m.isProgramChange()) tsf_channel_set_presetnumber (sf, ch, m.getProgramChangeNumber(), ch == 9);
    else if (m.isPitchWheel()) tsf_channel_set_pitchwheel (sf, ch, m.getPitchWheelValue());
    else if (m.isController()) tsf_channel_midi_control (sf, ch, m.getControllerNumber(), m.getControllerValue());
}

void SoundFontPlayer::renderInto (float* l, float* r, int n, float gain)
{
    while (n > 0)
    {
        const int chunk = juce::jmin (n, (int) buffer.size() / 2);
        tsf_render_float (sf, buffer.data(), chunk, 0);
        for (int i = 0; i < chunk; ++i) { l[i] += buffer[(size_t) i * 2] * gain; r[i] += buffer[(size_t) i * 2 + 1] * gain; }
        l += chunk; r += chunk; n -= chunk;
    }
}

void SoundFontPlayer::process (const juce::MidiBuffer& events, float* left, float* right, int numSamples, float gain)
{
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked() || sf == nullptr || buffer.empty()) return;
    int pos = 0;
    for (const auto meta : events)   // render in segments so every event lands on its sample
    {
        const int at = juce::jlimit (0, numSamples, meta.samplePosition);
        if (at > pos) { renderInto (left + pos, right + pos, at - pos, gain); pos = at; }
        handle (meta.getMessage());
    }
    if (pos < numSamples) renderInto (left + pos, right + pos, numSamples - pos, gain);
}

int SoundFontPlayer::activeVoices() const { return sf != nullptr ? tsf_active_voice_count (sf) : 0; }

void SoundFontPlayer::reset (const std::array<int, 16>& programs)
{
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked() || sf == nullptr) return;
    for (int ch = 0; ch < 16; ++ch)
    {
        tsf_channel_sounds_off_all (sf, ch);
        tsf_channel_set_sustain (sf, ch, 0);          // CC121 in TinySoundFont leaves the sustain pedal on: a pedal left down by the
        tsf_channel_set_pitchwheel (sf, ch, 8192);    // previous file would hold every note of the next one (and keep its bend)
        tsf_channel_midi_control (sf, ch, 121, 0);   // reset controllers
        tsf_channel_set_bank (sf, ch, 0);
        tsf_channel_set_presetnumber (sf, ch, programs[(size_t) ch], ch == 9);
    }
}
