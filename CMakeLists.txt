cmake_minimum_required(VERSION 3.22)
project(MIDIARCHIV VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_DIR "$ENV{USERPROFILE}/JUCE" CACHE PATH "Path to the JUCE source tree")
add_subdirectory(${JUCE_DIR} JUCE)

set(MA_CORE
    Source/MidiData.cpp
    Source/Archive.cpp
    Source/Lang.cpp
    Source/WebLookup.cpp)

set(MA_UI
    Source/ArchiveTable.cpp
    Source/DupView.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp
    Source/SoundEngine.cpp
    Source/SoundFontPlayer.cpp)

juce_add_plugin(MIDIARCHIV
    COMPANY_NAME "Un(d)AbtanzBAR"
    PRODUCT_NAME "Midi-Fundus"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE Yako
    PLUGIN_CODE MdAr
    VST3_CATEGORIES Instrument Generator
    FORMATS VST3 Standalone
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(MIDIARCHIV PRIVATE ${MA_UI} ${MA_CORE})

set(MA_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_definitions(MIDIARCHIV PUBLIC ${MA_DEFS})

if (MSVC)
    target_compile_options(MIDIARCHIV PRIVATE /utf-8)
endif()

target_link_libraries(MIDIARCHIV
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Archive / parser regression test (console)
juce_add_console_app(ArchivTest PRODUCT_NAME "ArchivTest" COMPANY_NAME "Un(d)AbtanzBAR")
target_sources(ArchivTest PRIVATE Tools/ArchivTest.cpp ${MA_CORE})
target_compile_definitions(ArchivTest PRIVATE ${MA_DEFS})
if (MSVC)
    target_compile_options(ArchivTest PRIVATE /utf-8)
endif()
target_link_libraries(ArchivTest PRIVATE juce::juce_audio_basics juce::juce_events juce::juce_data_structures PUBLIC juce::juce_recommended_config_flags)

# Renders the editor to PNG files (UI checks without a DAW) and writes the icon
juce_add_gui_app(UiShot PRODUCT_NAME "UiShot" COMPANY_NAME "Un(d)AbtanzBAR")
target_sources(UiShot PRIVATE Tools/UiShot.cpp ${MA_UI} ${MA_CORE})
target_compile_definitions(UiShot PRIVATE ${MA_DEFS} JucePlugin_Name="Midi-Fundus" JucePlugin_VersionString="${PROJECT_VERSION}")
if (MSVC)
    target_compile_options(UiShot PRIVATE /utf-8)
endif()
target_link_libraries(UiShot PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)

# Minimal VST3 host test (loads the built plugin like a DAW)
juce_add_console_app(HostTest PRODUCT_NAME "HostTest" COMPANY_NAME "Un(d)AbtanzBAR")
target_sources(HostTest PRIVATE Tools/HostTest.cpp)
target_compile_definitions(HostTest PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(HostTest PRIVATE juce::juce_audio_processors juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags)

# GM SoundFont (GeneralUser GS) next to the standalone, inside the VST3 bundle and next to UiShot
set(MA_SF2 "${CMAKE_CURRENT_SOURCE_DIR}/Resources/SoundFont/GeneralUser-GS.sf2")
set(MA_SF2_LIC "${CMAKE_CURRENT_SOURCE_DIR}/Resources/SoundFont/LICENSE_GeneralUser.txt")
add_custom_command(TARGET MIDIARCHIV_Standalone POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MA_SF2}" "${MA_SF2_LIC}" "$<TARGET_FILE_DIR:MIDIARCHIV_Standalone>/")
add_custom_command(TARGET MIDIARCHIV_VST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/MIDIARCHIV_artefacts/$<CONFIG>/VST3/Midi-Fundus.vst3/Contents/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MA_SF2}" "${MA_SF2_LIC}" "${CMAKE_BINARY_DIR}/MIDIARCHIV_artefacts/$<CONFIG>/VST3/Midi-Fundus.vst3/Contents/Resources/")
add_custom_command(TARGET UiShot POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MA_SF2}" "$<TARGET_FILE_DIR:UiShot>/")
