// MIDIARCHIV – small Standard MIDI File writer used by all format converters.
// Events are collected with absolute ticks per track and written as SMF type 1 (track 0 = conductor).
#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <vector>

namespace ma
{
class SmfBuilder
{
public:
    explicit SmfBuilder (int ticksPerQuarter = 480) : ppq (ticksPerQuarter) {}

    const int ppq;

    int addTrack (const juce::String& name = {})
    {
        tracks.emplace_back();
        const int t = (int) tracks.size() - 1;
        if (name.isNotEmpty()) meta (t, 0, 0x03, name);
        return t;
    }
    int numTracks() const { return (int) tracks.size(); }

    void meta (int track, juce::int64 tick, int type, const juce::String& text, int order = 0)
    {
        const auto* utf = text.toRawUTF8();
        meta (track, tick, type, utf, strlen (utf), order);
    }
    void meta (int track, juce::int64 tick, int type, const void* data, size_t len, int order = 0)
    {
        juce::MemoryOutputStream os;
        os.writeByte ((char) 0xFF); os.writeByte ((char) type);
        putVar (os, (juce::uint32) len);
        os.write (data, len);
        add (track, tick, os.getMemoryBlock(), order);
    }
    void tempo (juce::int64 tick, double bpm)
    {
        const auto us = (juce::uint32) juce::jlimit (1, 0xFFFFFF, juce::roundToInt (60000000.0 / juce::jlimit (5.0, 1000.0, bpm)));
        const juce::uint8 d[3] = { (juce::uint8) (us >> 16), (juce::uint8) (us >> 8), (juce::uint8) us };
        meta (0, tick, 0x51, d, 3, 1);
    }
    void timeSig (juce::int64 tick, int num, int den)
    {
        int pow2 = 0;
        while ((1 << pow2) < den && pow2 < 7) ++pow2;
        const juce::uint8 d[4] = { (juce::uint8) juce::jlimit (1, 64, num), (juce::uint8) pow2, 24, 8 };
        meta (0, tick, 0x58, d, 4, 1);
    }
    void text (int track, juce::int64 tick, const juce::String& t)  { meta (track, tick, 0x01, t); }
    void lyric (int track, juce::int64 tick, const juce::String& t) { meta (track, tick, 0x05, t, 3); }
    void channelEvent (int track, juce::int64 tick, int status, int d1, int d2 = -1, int order = 2)
    {
        juce::MemoryBlock mb;
        const juce::uint8 b[3] = { (juce::uint8) status, (juce::uint8) (d1 & 0x7F), (juce::uint8) (juce::jmax (0, d2) & 0x7F) };
        mb.append (b, d2 < 0 ? 2 : 3);
        add (track, tick, mb, order);
    }
    void program (int track, juce::int64 tick, int ch, int prog) { channelEvent (track, tick, 0xC0 | (ch & 15), juce::jlimit (0, 127, prog), -1, 1); }
    void controller (int track, juce::int64 tick, int ch, int cc, int v) { channelEvent (track, tick, 0xB0 | (ch & 15), cc, juce::jlimit (0, 127, v), 1); }
    void pitchBend (int track, juce::int64 tick, int ch, int value14) { channelEvent (track, tick, 0xE0 | (ch & 15), value14 & 0x7F, (value14 >> 7) & 0x7F, 2); }
    void note (int track, int ch, juce::int64 on, juce::int64 off, int pitch, int vel)
    {
        if (pitch < 0 || pitch > 127) return;
        on = juce::jmax<juce::int64> (0, on);
        off = juce::jmax (on + 1, off);
        channelEvent (track, on, 0x90 | (ch & 15), pitch, juce::jlimit (1, 127, vel), 4);
        channelEvent (track, off, 0x80 | (ch & 15), pitch, 0, 0);
    }
    void raw (int track, juce::int64 tick, const juce::MemoryBlock& bytes, int order = 2) { add (track, tick, bytes, order); }
    void endAt (juce::int64 tick) { endTick = juce::jmax (endTick, tick); }   // keeps a song length without notes

    bool hasNotes() const { return noteCount > 0; }

    juce::MemoryBlock build() const
    {
        juce::MemoryOutputStream file;
        file.write ("MThd", 4);
        file.writeIntBigEndian (6);
        file.writeShortBigEndian (1);
        file.writeShortBigEndian ((short) tracks.size());
        file.writeShortBigEndian ((short) ppq);
        for (size_t t = 0; t < tracks.size(); ++t)
        {
            auto evs = tracks[t];
            std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });
            juce::MemoryOutputStream tr;
            juce::int64 last = 0;
            for (auto& e : evs)
            {
                putVar (tr, (juce::uint32) (e.tick - last));
                last = e.tick;
                tr.write (e.bytes.getData(), e.bytes.getSize());
            }
            putVar (tr, (juce::uint32) (t == 0 ? juce::jmax<juce::int64> (0, endTick - last) : 0));
            tr.writeByte ((char) 0xFF); tr.writeByte (0x2F); tr.writeByte (0);
            file.write ("MTrk", 4);
            file.writeIntBigEndian ((int) tr.getDataSize());
            file.write (tr.getData(), tr.getDataSize());
        }
        return file.getMemoryBlock();
    }

    static void putVar (juce::OutputStream& os, juce::uint32 v)
    {
        juce::uint8 buf[5];
        int n = 0;
        buf[n++] = (juce::uint8) (v & 0x7F);
        while ((v >>= 7) != 0) buf[n++] = (juce::uint8) (0x80 | (v & 0x7F));
        while (n > 0) os.writeByte ((char) buf[--n]);
    }

private:
    struct Ev { juce::int64 tick; int order; juce::MemoryBlock bytes; };
    std::vector<std::vector<Ev>> tracks { 1 };
    juce::int64 endTick = 0;
    int noteCount = 0;

    void add (int track, juce::int64 tick, const juce::MemoryBlock& bytes, int order)
    {
        while ((int) tracks.size() <= track) tracks.emplace_back();
        tracks[(size_t) track].push_back ({ juce::jmax<juce::int64> (0, tick), order, bytes });
        endTick = juce::jmax (endTick, tick);
        if (bytes.getSize() == 3 && (static_cast<const juce::uint8*> (bytes.getData())[0] & 0xF0) == 0x90) ++noteCount;
    }
};
} // namespace ma
