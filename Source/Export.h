// MIDIARCHIV – "Convert to …": writes any readable file as Standard MIDI File, MusicXML or ABC.
//   MIDI      (.mid, SMF type 1)       – every DAW (Ableton Live, Studio One / Fender Studio Pro, Reason, FL Studio, Cubase …)
//   MusicXML  (.musicxml, version 3.1) – notation programs (MuseScore 3 and later, Finale, Sibelius, Dorico …)
//   ABC       (.abc, version 2.1)      – one voice per track, for folk / tune software and abcjs
//   DAWproject (.dawproject, 1.0)      – Bitwig 5+, Studio One 6.5+ / Fender Studio Pro, Cubase 14+ (ExportDaw.cpp)
//   REAPER    (.rpp)                   – REAPER project, one MIDI item per track (ExportDaw.cpp)
// Notes are quantised for the notation formats (sixteenths and triplets), long notes are split at bar lines and tied.
#pragma once
#include <juce_core/juce_core.h>

namespace ma
{
enum class ExportFormat { midi, musicXml, abc, dawProject, reaper };

juce::String exportExtension (ExportFormat f);
juce::String exportFormatName (ExportFormat f);

// Writes <dest folder>/<Artist - Title>.<ext> (an existing file is never overwritten, a number is added).
bool exportFile (const juce::File& source, ExportFormat format, const juce::File& destFolder, juce::File& written, juce::String& error);

// For tests: conversion into memory.
bool exportToMemory (const juce::MemoryBlock& sourceData, const juce::String& sourceName, ExportFormat format,
                     juce::MemoryBlock& out, juce::String& suggestedName, juce::String& error);

struct MidiInfo;
namespace daw
{
bool writeDawProject (const juce::MemoryBlock& smf, const MidiInfo& info, juce::MemoryBlock& out, juce::String& error);
bool writeReaper (const juce::MemoryBlock& smf, const MidiInfo& info, juce::MemoryBlock& out, juce::String& error);
}
} // namespace ma
