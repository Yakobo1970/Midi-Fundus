#include "GmSamples.h"
#include "Lang.h"
#include <map>
#include <mutex>

#if defined (_MSC_VER)
 #pragma warning (push, 0)
#endif
#define TSF_STATIC            // own static copy of TinySoundFont (the player has its own in SoundFontPlayer.cpp)
#define TSF_IMPLEMENTATION
#include "ThirdParty/tsf.h"
#if defined (_MSC_VER)
 #pragma warning (pop)
#endif

namespace ma::gm
{
namespace
{
// one loaded SoundFont per process (32 MB; loaded on the first export)
tsf* soundFont (const juce::File& file, juce::String& error)
{
    static std::mutex m;
    static juce::File loadedFile;
    static tsf* loaded = nullptr;
    const std::lock_guard<std::mutex> lock (m);
    if (loaded != nullptr && loadedFile == file) return loaded;
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data)) { error = T ("SoundFont nicht lesbar: %1", file.getFullPathName()); return nullptr; }
    tsf* fresh = tsf_load_memory (data.getData(), (int) data.getSize());
    if (fresh == nullptr) { error = T ("SoundFont nicht lesbar: %1", file.getFullPathName()); return nullptr; }
    if (loaded != nullptr) tsf_close (loaded);
    loaded = fresh;
    loadedFile = file;
    return loaded;
}
} // namespace

juce::File findSoundFont()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("MIDIARCHIV_SF2", {});
    if (env.isNotEmpty() && juce::File (env).existsAsFile()) return juce::File (env);
    const auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    const auto module = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
    juce::Array<juce::File> candidates { exeDir.getChildFile ("GeneralUser-GS.sf2"),
                                         exeDir.getChildFile ("SoundFont/GeneralUser-GS.sf2"),
                                         exeDir.getParentDirectory().getChildFile ("Resources/GeneralUser-GS.sf2"),        // inside Midi-Fundus.vst3
                                         module.getChildFile ("Contents/Resources/GeneralUser-GS.sf2"),
                                         juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MIDIARCHIV/GeneralUser-GS.sf2") };
    for (auto d = exeDir; d.getParentDirectory() != d; d = d.getParentDirectory())   // source tree (tests)
        candidates.add (d.getChildFile ("Resources/SoundFont/GeneralUser-GS.sf2"));
    for (auto& f : candidates)
        if (f.existsAsFile()) return f;
    return {};
}

bool extract (const juce::File& sf2, int bank, int program, const std::set<int>& keys, Preset& out, juce::String& error)
{
    out = {};
    auto* f = soundFont (sf2, error);
    if (f == nullptr) return false;
    int index = tsf_get_presetindex (f, bank, program);
    if (index < 0 && bank == 128) index = tsf_get_presetindex (f, 128, 0);   // unknown kit: standard kit
    if (index < 0 && bank != 0) index = tsf_get_presetindex (f, 0, program);
    if (index < 0) { error = T ("Kein Klang im SoundFont für Programm %1", program + 1); return false; }
    const auto& p = f->presets[index];
    out.name = juce::String (p.presetName).trim();
    std::map<unsigned int, int> waveOf;   // sample offset -> wave index
    for (int r = 0; r < p.regionNum; ++r)
    {
        const auto& rg = p.regions[r];
        bool used = keys.empty();
        for (int k : keys) used |= k >= rg.lokey && k <= rg.hikey;
        if (! used || rg.end <= rg.offset) continue;
        int w;
        if (auto it = waveOf.find (rg.offset); it != waveOf.end()) w = it->second;
        else
        {
            Wave wave;
            wave.rate = (int) rg.sample_rate;
            wave.root = rg.pitch_keycenter;
            wave.id = rg.offset;
            const auto len = (size_t) (rg.end - rg.offset + 1);
            wave.pcm.assign (f->fontSamples + rg.offset, f->fontSamples + rg.offset + len);
            w = (int) out.waves.size();
            out.waves.push_back (std::move (wave));
            waveOf[rg.offset] = w;
        }
        Zone z;
        z.wave = w;
        z.loKey = rg.lokey; z.hiKey = rg.hikey; z.loVel = rg.lovel; z.hiVel = rg.hivel;
        // played pitch = key - keycenter + transpose + tune/100  ->  Ableton root key + detune (±50 cents)
        const int semis = (int) std::lround (rg.tune / 100.0);
        z.detuneCents = (float) (rg.tune - semis * 100);
        z.rootKey = rg.pitch_keycenter - rg.transpose - semis;
        if (rg.pitch_keytrack == 0 && rg.lokey == rg.hikey) z.rootKey = rg.lokey - rg.transpose - semis;   // fixed pitch (drums)
        z.rootKey = juce::jlimit (0, 127, z.rootKey);
        z.keyTrack = rg.pitch_keytrack;
        z.transpose = rg.transpose;
        z.tune = rg.tune;
        z.gainDb = -rg.attenuation;
        z.pan = juce::jlimit (-1.0f, 1.0f, rg.pan * 2.0f);
        const int len = (int) out.waves[(size_t) w].pcm.size();
        z.loop = rg.loop_mode != TSF_LOOPMODE_NONE && rg.loop_end > rg.loop_start;
        z.loopStart = juce::jlimit (0, len - 1, (int) rg.loop_start - (int) rg.offset);
        // TinySoundFont keeps the last sample of the loop; Ableton's loop end is the first sample after it.
        // One sample off detunes short loops audibly (a 32-sample loop played as 31 = +55 cents).
        z.loopEnd = juce::jlimit (0, len, (int) rg.loop_end - (int) rg.offset + 1);
        if (z.loopEnd <= z.loopStart) z.loop = false;
        z.attack = rg.ampenv.delay + rg.ampenv.attack;
        // key-dependent decay is kept in timecents by TinySoundFont: take it at the zone's root key
        z.decay = rg.ampenv.keynumToDecay != 0 ? std::pow (2.0f, (rg.ampenv.decay + rg.ampenv.keynumToDecay * (60.0f - (float) rg.pitch_keycenter)) / 1200.0f)
                                               : rg.ampenv.decay;
        z.sustain = rg.ampenv.sustain;
        z.release = rg.ampenv.release;
        out.zones.push_back (z);
    }
    if (out.zones.empty()) { error = T ("Kein Klang im SoundFont für Programm %1", program + 1); return false; }
    return true;
}

bool writeWav (const Wave& w, const juce::File& file)
{
    juce::MemoryOutputStream os;
    const auto frames = (juce::uint32) w.pcm.size();
    os.write ("RIFF", 4); os.writeInt ((int) (36 + frames * 2)); os.write ("WAVE", 4);
    os.write ("fmt ", 4); os.writeInt (16); os.writeShort (1); os.writeShort (1);
    os.writeInt (w.rate); os.writeInt (w.rate * 2); os.writeShort (2); os.writeShort (16);
    os.write ("data", 4); os.writeInt ((int) (frames * 2));
    for (float s : w.pcm) os.writeShort ((short) juce::jlimit (-32768, 32767, (int) std::lround (s * 32767.0f)));
    return file.replaceWithData (os.getData(), os.getDataSize());
}
} // namespace ma::gm
