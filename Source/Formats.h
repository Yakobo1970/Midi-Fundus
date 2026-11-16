// MIDIARCHIV – non-MIDI formats. Every reader converts its file into a Standard MIDI File in memory, so analysis,
// duplicate search, preview and piano roll work unchanged. Metadata found in the file overrides what the SMF yields.
//
//  Games / retro:   XMI (Miles), MUS (DMX/Doom), HMP (HMI), CMF (Creative), MMF (Yamaha SMAF)
//  Sequencers:      WRK (Cakewalk), FLP (FL Studio), ALS (Ableton Live), SONG (Studio One / Fender Studio Pro),
//                   REASON / RPS (Reason – only tempo, metre, length: the song data is encrypted)
//  Open projects:   DAWPROJECT (Bitwig, Studio One 6.5+ / Fender Studio Pro, Cubase 14+), RPP (REAPER)
//  Notation:        MusicXML (.musicxml .xml .mxl), MuseScore (.mscz .mscx), ABC (.abc), Humdrum (.krn)
//  Karaoke:         UltraStar (.txt)
#pragma once
#include "MidiData.h"

namespace ma
{
struct ConvertedSong
{
    juce::String format;            // shown in the "Format" column, e.g. "XMI", "MusicXML"
    juce::MemoryBlock smf;
    juce::String title, artist, composer, genre, copyright, comment, language, lyrics;
    int year = 0;
    double seconds = 0;             // length when the file has no notes (Reason)
    bool writable = false;          // header fields can be written back (UltraStar)
};

// File name / content checks for the scanner (ambiguous extensions are sniffed).
bool isConvertibleFile (const juce::File& f);
juce::String formatNameForFile (const juce::String& fileName);   // empty for MIDI
// Reason songs: the notes are encrypted, so there is nothing to play, show, drag or convert.
bool hasLockedNotes (const juce::String& fileName);
juce::String lockedNotesMessage();

// Detects the format by content and converts. False if unknown or unreadable (error filled where possible).
bool convertToSmf (const juce::MemoryBlock& data, ConvertedSong& out, juce::String& error);

// Converted MIDI copy of any supported file (for the DAW / "convert to"); empty File on failure.
juce::File midiCopyOf (const juce::File& src, const juce::File& folder);

namespace fmt   // individual readers (Formats*.cpp)
{
bool readUltraStar (const juce::MemoryBlock&, ConvertedSong&, juce::String& error);
bool readXmi (const juce::MemoryBlock&, ConvertedSong&, juce::String& error);
bool readMus (const juce::MemoryBlock&, ConvertedSong&, juce::String& error);
bool readHmp (const juce::MemoryBlock&, ConvertedSong&, juce::String& error);
bool readCmf (const juce::MemoryBlock&, ConvertedSong&, juce::String& error);
bool readMmf (const juce::MemoryBlock&, ConvertedSong&, juce::String& error);
bool readWrk (const juce::MemoryBlock&, ConvertedSong&, juce::String& error);
bool readFlp (const juce::MemoryBlock&, ConvertedSong&, juce::String& error);
bool readAls (const juce::MemoryBlock&, ConvertedSong&, juce::String& error);
bool readStudioOne (juce::ZipFile&, ConvertedSong&, juce::String& error);
bool readReason (const juce::MemoryBlock&, ConvertedSong&, juce::String& error);
bool readDawProject (juce::ZipFile&, ConvertedSong&, juce::String& error);
bool readReaper (const juce::String& text, ConvertedSong&, juce::String& error);
bool readMusicXml (const juce::String& xml, ConvertedSong&, juce::String& error);
bool readMuseScore (const juce::String& xml, ConvertedSong&, juce::String& error);
bool readAbc (const juce::String& text, ConvertedSong&, juce::String& error);
bool readKern (const juce::String& text, ConvertedSong&, juce::String& error);

// helpers shared by the readers
juce::String decodeText (const void* data, size_t size);   // UTF-8 (with/without BOM) or Windows-1252
std::unique_ptr<juce::XmlElement> parseXml (const juce::String& text);
juce::String zipEntryText (juce::ZipFile& zip, const juce::String& name);
juce::MemoryBlock zipEntryData (juce::ZipFile& zip, int index);
} // namespace fmt
} // namespace ma
