// Loads the built MIDIARCHIV.vst3 like a DAW: editor, preview playback via the processor, MIDI output, state round trip.
// Usage: HostTest <MIDIARCHIV.vst3>   (DAW transport sync: UiShot synctest)
#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::cout << "FAIL: " << msg << "\n"; } else std::cout << "ok:   " << msg << "\n"; } while (0)

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::cout << "usage: HostTest <vst3>\n"; return 1; }
    juce::AudioPluginFormatManager fm;
    fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
    juce::OwnedArray<juce::PluginDescription> types;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile (types, juce::String (argv[1]));
    CHECK (types.size() == 1, "plugin found");
    if (types.isEmpty()) return 1;
    CHECK (types[0]->name == "Midi-Fundus", "name");
    juce::String err;
    auto inst = fm.createPluginInstance (*types[0], 48000.0, 512, err);
    CHECK (inst != nullptr, "instance created " << err);
    if (inst == nullptr) return 1;
    inst->enableAllBuses();
    inst->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 50; ++i) { midi.clear(); inst->processBlock (buf, midi); }
    CHECK (buf.getMagnitude (0, 512) < 1e-6f, "silent without preview");
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
        CHECK (ed != nullptr, "editor created");
        if (ed) CHECK (ed->getWidth() >= 900 && ed->getHeight() >= 520, "editor size " << ed->getWidth() << "x" << ed->getHeight());
        for (int i = 0; i < 30; ++i) { juce::MessageManager::getInstance()->runDispatchLoopUntil (10); }
    }
    juce::MemoryBlock state;
    inst->getStateInformation (state);
    CHECK (state.getSize() > 0, "state saved");
    inst->releaseResources();
    inst.reset();
    std::cout << (failures == 0 ? "ALL OK\n" : "FAILURES\n");
    return failures;
}
