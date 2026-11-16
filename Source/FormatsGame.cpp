// MIDIARCHIV – game and retro formats: XMI (Miles Sound System), MUS (DMX / Doom), HMP (Human Machine Interfaces),
// CMF (Creative Music File), MMF (Yamaha SMAF, see FormatsSmaf.cpp).
#include "Formats.h"
#include "SmfBuilder.h"
#include "Lang.h"
#include <map>

namespace ma::fmt
{
namespace
{
juce::uint32 le16 (const juce::uint8* p) { return (juce::uint32) (p[0] | (p[1] << 8)); }
juce::uint32 le32 (const juce::uint8* p) { return (juce::uint32) (p[0] | (p[1] << 8) | (p[2] << 16) | ((juce::uint32) p[3] << 24)); }
juce::uint32 be32 (const juce::uint8* p) { return (juce::uint32) (((juce::uint32) p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); }

bool readVar (const juce::uint8*& p, const juce::uint8* end, juce::uint32& v)
{
    v = 0;
    for (int i = 0; i < 5; ++i)
    {
        if (p >= end) return false;
        const auto b = *p++;
        v = (v << 7) | (b & 0x7F);
        if ((b & 0x80) == 0) return true;
    }
    return false;
}

// Track per MIDI channel, created on first use.
struct ChannelTracks
{
    SmfBuilder& b;
    int track[16] { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    int operator() (int ch) { ch &= 15; if (track[ch] < 0) track[ch] = b.addTrack(); return track[ch]; }
};

// Standard MIDI track stream (running status). deltaReader reads one delta time; returns false at the end.
template <typename DeltaReader>
void parseMidiStream (const juce::uint8* p, const juce::uint8* end, SmfBuilder& b, ChannelTracks& tracks, double tickScale, DeltaReader readDelta,
                      int conductorTrack = 0)
{
    juce::uint64 tick = 0;
    int running = 0;
    while (p < end)
    {
        juce::uint32 delta = 0;
        if (! readDelta (p, end, delta)) return;
        tick += delta;
        if (p >= end) return;
        int st = *p;
        if (st & 0x80) { ++p; if (st < 0xF0) running = st; }
        else if (running != 0) st = running;
        else return;
        const auto t = (juce::int64) (tick * tickScale);
        if (st == 0xFF)
        {
            if (p >= end) return;
            const int type = *p++;
            juce::uint32 len = 0;
            if (! readVar (p, end, len) || p + len > end) return;
            if (type == 0x2F) return;
            if (type == 0x51 || type == 0x58 || type == 0x59) b.meta (conductorTrack, t, type, p, len, 1);
            else if (type >= 0x01 && type <= 0x07) b.meta (type == 0x05 ? conductorTrack : conductorTrack, t, type, p, len);
            p += len;
            continue;
        }
        if (st == 0xF0 || st == 0xF7)
        {
            juce::uint32 len = 0;
            if (! readVar (p, end, len) || p + len > end) return;
            p += len;
            continue;
        }
        const int hi = st & 0xF0, ch = st & 15;
        const int nData = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
        if (p + nData > end) return;
        const int d1 = p[0], d2 = nData > 1 ? p[1] : -1;
        p += nData;
        b.channelEvent (tracks (ch), t, st, d1, d2, hi == 0x80 || (hi == 0x90 && d2 == 0) ? 0 : (hi == 0x90 ? 4 : 1));
    }
}
} // namespace

// ============================================================================================ XMI
// IFF container (FORM XDIR + CAT XMID or a single FORM XMID). EVNT data: delays are runs of bytes < 0x80 (added up),
// note-on carries its duration as a variable-length number, no note-offs. Fixed 120 Hz timebase.
bool readXmi (const juce::MemoryBlock& data, ConvertedSong& out, juce::String& error)
{
    auto* d = static_cast<const juce::uint8*> (data.getData());
    const size_t n = data.getSize();
    // find the first EVNT chunk inside a FORM XMID
    const juce::uint8* evnt = nullptr;
    juce::uint32 evLen = 0;
    for (size_t i = 0; i + 8 <= n; ++i)
        if (memcmp (d + i, "EVNT", 4) == 0)
        {
            evLen = be32 (d + i + 4);
            if (i + 8 + evLen <= n) { evnt = d + i + 8; break; }
        }
    if (evnt == nullptr) { error = T ("XMI ohne Ereignisdaten (EVNT)"); return false; }

    SmfBuilder b (60);   // 60 ticks per quarter at 120 BPM = 120 ticks per second
    b.tempo (0, 120.0);
    ChannelTracks tracks { b };
    const auto* p = evnt;
    const auto* end = evnt + evLen;
    juce::int64 t = 0;
    while (p < end)
    {
        if (*p < 0x80) { while (p < end && *p < 0x80) t += *p++; continue; }
        const int st = *p++;
        const int hi = st & 0xF0, ch = st & 15;
        if (st == 0xFF)
        {
            if (p >= end) break;
            const int type = *p++;
            juce::uint32 len = 0;
            if (! readVar (p, end, len) || p + len > end) break;
            if (type == 0x2F) break;
            if (type == 0x58 || (type >= 0x01 && type <= 0x07)) b.meta (0, t, type, p, len);
            p += len;
            continue;
        }
        if (st == 0xF0 || st == 0xF7)
        {
            juce::uint32 len = 0;
            if (! readVar (p, end, len) || p + len > end) break;
            p += len;
            continue;
        }
        if (hi == 0x90)
        {
            if (p + 2 > end) break;
            const int key = p[0], vel = p[1];
            p += 2;
            juce::uint32 dur = 0;
            if (! readVar (p, end, dur)) break;
            if (vel > 0) b.note (tracks (ch), ch, t, t + (juce::int64) dur, key, vel);
            continue;
        }
        const int nData = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
        if (p + nData > end) break;
        const int d1 = p[0], d2 = nData > 1 ? p[1] : -1;
        p += nData;
        if (hi == 0xB0 && d1 >= 0x6E && d1 <= 0x78) continue;   // XMI-specific controllers (loops, banks, callbacks)
        if (hi != 0x80) b.channelEvent (tracks (ch), t, st, d1, d2, 1);
    }
    if (! b.hasNotes()) { error = T ("Keine Noten gefunden"); return false; }
    out.format = "XMI";
    out.smf = b.build();
    return true;
}

// ============================================================================================ MUS (DMX)
bool readMus (const juce::MemoryBlock& data, ConvertedSong& out, juce::String& error)
{
    auto* d = static_cast<const juce::uint8*> (data.getData());
    const size_t n = data.getSize();
    if (n < 16) { error = T ("Datei nicht lesbar"); return false; }
    const auto scoreLen = le16 (d + 4), scoreStart = le16 (d + 6);
    if (scoreStart >= n) { error = T ("Datei nicht lesbar"); return false; }
    const auto* p = d + scoreStart;
    const auto* end = d + juce::jmin<size_t> (n, (size_t) scoreStart + (scoreLen > 0 ? scoreLen : n));

    SmfBuilder b (70);   // 140 ticks per second
    b.tempo (0, 120.0);
    ChannelTracks tracks { b };
    static const int ctrlMap[10] = { -1, 0, 1, 7, 10, 11, 91, 93, 64, 67 };
    static const int sysMap[5] = { 120, 123, 126, 127, 121 };
    int velocity[16];
    std::fill (std::begin (velocity), std::end (velocity), 127);
    std::map<int, juce::int64> playing;   // (ch << 8 | key) -> start
    juce::int64 t = 0;
    auto mapCh = [] (int c) { return c == 15 ? 9 : (c >= 9 ? c + 1 : c); };
    while (p < end)
    {
        const int desc = *p++;
        const int type = (desc >> 4) & 7, ch = mapCh (desc & 15);
        auto byte = [&] { return p < end ? (int) *p++ : 0; };
        switch (type)
        {
            case 0: { const int key = byte() & 0x7F; const int k = ch << 8 | key;
                      if (auto it = playing.find (k); it != playing.end()) { b.note (tracks (ch), ch, it->second, t, key, velocity[ch]); playing.erase (it); } break; }
            case 1: { const int v = byte(); if (v & 0x80) velocity[ch] = byte() & 0x7F;
                      const int key = v & 0x7F; const int k = ch << 8 | key;
                      if (auto it = playing.find (k); it != playing.end()) { b.note (tracks (ch), ch, it->second, t, key, velocity[ch]); playing.erase (it); }
                      if (velocity[ch] > 0) playing[k] = t;
                      break; }
            case 2: b.pitchBend (tracks (ch), t, ch, juce::jlimit (0, 16383, byte() * 64)); break;
            case 3: { const int s = byte(); if (s >= 10 && s <= 14) b.controller (tracks (ch), t, ch, sysMap[s - 10], 0); break; }
            case 4: { const int c = byte(), v = byte() & 0x7F;
                      if (c == 0) b.program (tracks (ch), t, ch, v);
                      else if (c < 10) b.controller (tracks (ch), t, ch, ctrlMap[c], v);
                      break; }
            case 5: break;          // end of measure
            case 6: p = end; break; // score end
            default: break;
        }
        if (desc & 0x80)
        {
            juce::uint32 delay = 0;
            if (! readVar (p, end, delay)) break;
            t += delay;
        }
    }
    for (auto& [k, start] : playing) b.note (tracks (k >> 8), k >> 8, start, t, k & 0xFF, velocity[k >> 8]);
    if (! b.hasNotes()) { error = T ("Keine Noten gefunden"); return false; }
    out.format = "MUS";
    out.smf = b.build();
    return true;
}

// ============================================================================================ HMP
// Header: 0x30 tracks, 0x34 division (unused for timing), 0x38 ticks per second (usually 120), 0x3C length in seconds; tracks at 0x308
// (0x388 for "HMIMIDIP013195"), each: u32 index, u32 size incl. header, u32 reserved, data. Delta times are
// variable-length numbers in reversed order (low 7-bit group first, last byte has bit 7 set).
bool readHmp (const juce::MemoryBlock& data, ConvertedSong& out, juce::String& error)
{
    auto* d = static_cast<const juce::uint8*> (data.getData());
    const size_t n = data.getSize();
    if (n < 0x40) { error = T ("Datei nicht lesbar"); return false; }
    const bool v2 = memcmp (d + 8, "013195", 6) == 0;
    const auto numTracks = le32 (d + 0x30);
    const int rate = (int) juce::jlimit (10u, 2000u, le32 (d + 0x38));   // ticks per second (checked against the length field)
    size_t pos = v2 ? 0x388 : 0x308;
    SmfBuilder b ((rate % 2) == 0 ? rate / 2 : rate);
    b.tempo (0, (rate % 2) == 0 ? 120.0 : 60.0);
    ChannelTracks tracks { b };
    auto readDelta = [] (const juce::uint8*& p, const juce::uint8* end, juce::uint32& v) {
        v = 0;
        for (int shift = 0; shift < 35; shift += 7)
        {
            if (p >= end) return false;
            const auto c = *p++;
            v |= (juce::uint32) (c & 0x7F) << shift;
            if (c & 0x80) return true;
        }
        return false;
    };
    for (juce::uint32 t = 0; t < numTracks && t < 64 && pos + 12 <= n; ++t)
    {
        const auto size = le32 (d + pos + 4);
        if (size < 12 || pos + size > n) break;
        parseMidiStream (d + pos + 12, d + pos + size, b, tracks, 1.0, readDelta);
        pos += size;
    }
    if (! b.hasNotes()) { error = T ("Keine Noten gefunden"); return false; }
    out.format = "HMP";
    out.smf = b.build();
    return true;
}

// ============================================================================================ CMF
bool readCmf (const juce::MemoryBlock& data, ConvertedSong& out, juce::String& error)
{
    auto* d = static_cast<const juce::uint8*> (data.getData());
    const size_t n = data.getSize();
    if (n < 40) { error = T ("Datei nicht lesbar"); return false; }
    const auto musicOff = le16 (d + 8), tpq = juce::jmax (1u, le16 (d + 10)), tps = juce::jmax (1u, le16 (d + 12));
    auto str = [&] (juce::uint32 off) -> juce::String {
        if (off == 0 || off >= n) return {};
        size_t e = off;
        while (e < n && d[e] != 0 && e - off < 256) ++e;
        return decodeText (d + off, e - off).trim();
    };
    out.title = str (le16 (d + 14));
    out.composer = str (le16 (d + 16));
    out.comment = str (le16 (d + 18));
    if (musicOff >= n) { error = T ("Datei nicht lesbar"); return false; }
    SmfBuilder b ((int) tpq);
    b.tempo (0, 60.0 * tps / tpq);
    ChannelTracks tracks { b };
    parseMidiStream (d + musicOff, d + n, b, tracks, 1.0, readVar);
    if (! b.hasNotes()) { error = T ("Keine Noten gefunden"); return false; }
    out.format = "CMF";
    out.smf = b.build();
    return true;
}
} // namespace ma::fmt
