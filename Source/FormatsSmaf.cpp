// MIDIARCHIV – Yamaha SMAF (.mmf), score tracks in "Handyphone Standard" format (format type 0, MA-1/MA-2 ring tones).
// Container: "MMMD" + size, chunks CNTI (contents info "ST:title,AN:…"), MTR1…MTR4 (4 monophonic channels each):
// format, sequence type, timebase for durations and gate times, channel status, then Mtsu (setup) and Mtsq (sequence).
// Sequence: [duration][event]; duration/gate: 1 byte, or 2 bytes ((b1 & 0x7F) << 7) + b2 + 128.
// Note event: [ch:2][octave:2][note:4] + gate; pitch = 36 + 12 * (octave + octave shift) + note.
// 0x00 [ch:2][type:6] value: 0x30 program, 0x31 bank (0x80 = drums: the program is the GM drum key), 0x32 octave shift
// (0..4, 0x81..0x84 = -1..-4), 0x37 volume; 0x00 [ch:2][xx][vvvv]: short controls; 0xFF 0xF0 len … exclusive; 00 00 00 00 end.
// Worked out against 774 SMAF/MIDI pairs of the same songs (timing to the millisecond, programs identical).
#include "Formats.h"
#include "SmfBuilder.h"
#include "Lang.h"

namespace ma::fmt
{
namespace
{
juce::uint32 be32s (const juce::uint8* p) { return ((juce::uint32) p[0] << 24) | ((juce::uint32) p[1] << 16) | ((juce::uint32) p[2] << 8) | p[3]; }
int timebaseMs (int code)
{
    switch (code)
    {
        case 0x00: return 1; case 0x01: return 2; case 0x02: return 4; case 0x03: return 5;
        case 0x10: return 10; case 0x11: return 20; case 0x12: return 40; case 0x13: return 50;
        default: return 4;
    }
}
}

bool readMmf (const juce::MemoryBlock& data, ConvertedSong& out, juce::String& error)
{
    auto* d = static_cast<const juce::uint8*> (data.getData());
    const size_t n = data.getSize();
    if (n < 16) { error = T ("Datei nicht lesbar"); return false; }
    SmfBuilder b (500);   // 500 ticks per quarter at 120 BPM = 1 ms per tick
    b.tempo (0, 120.0);
    int tracksByChannel[16];
    std::fill (std::begin (tracksByChannel), std::end (tracksByChannel), -1);
    bool unsupported = false;
    size_t pos = 8;
    while (pos + 8 <= n)
    {
        const auto len = be32s (d + pos + 4);
        const auto* c = d + pos + 8;
        if (pos + 8 + len > n) break;
        if (memcmp (d + pos, "CNTI", 4) == 0 && len > 5)
        {
            juce::StringArray tags;
            tags.addTokens (decodeText (c + 5, len - 5), ",", "");
            for (auto& t : tags)
            {
                if (t.length() < 4 || t[2] != ':') continue;
                const auto key = t.substring (0, 2), v = t.substring (3).trim();
                if (v.isEmpty()) continue;
                if (key == "ST") out.title = v;
                else if (key == "AN") out.artist = v;
                else if (key == "SW") out.composer = v;
                else if (key == "CR") out.copyright = v;
                else if (key == "WW" && out.artist.isEmpty()) out.artist = v;
            }
        }
        else if (memcmp (d + pos, "MTR", 3) == 0 && len >= 6)
        {
            const int trackNo = d[pos + 3];
            if (c[0] != 0) { unsupported = true; pos += 8 + len; continue; }   // Mobile Standard (MA-3/5) not supported
            const int tbD = timebaseMs (c[2]), tbG = timebaseMs (c[3]);
            size_t q = 6;
            while (q + 8 <= len)
            {
                const auto sl = be32s (c + q + 4);
                if (q + 8 + sl > len) break;
                if (memcmp (c + q, "Mtsq", 4) == 0)
                {
                    const auto* s = c + q + 8;
                    const auto* e = s + sl;
                    int program[4] = { 0, 0, 0, 0 }, octShift[4] = { 0, 0, 0, 0 }, volume[4] = { 100, 100, 100, 100 };
                    bool drums[4] = { false, false, false, false };
                    juce::int64 t = 0;
                    auto var = [&e] (const juce::uint8*& p, int& v) {
                        if (p >= e) return false;
                        if (*p & 0x80) { if (p + 1 >= e) return false; v = ((p[0] & 0x7F) << 7) + p[1] + 128; p += 2; }
                        else v = *p++;
                        return true;
                    };
                    const auto* p = s;
                    while (p < e)
                    {
                        if (p + 4 <= e && p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0) break;
                        int dur = 0;
                        if (! var (p, dur) || p >= e) break;
                        t += (juce::int64) dur * tbD;
                        const int ev = *p++;
                        if (ev == 0x00)
                        {
                            if (p >= e) break;
                            const int b2 = *p++;
                            const int ch = b2 >> 6;
                            if ((b2 & 0x30) == 0x30)
                            {
                                if (p >= e) break;
                                const int v = *p++;
                                switch (b2 & 0x3F)
                                {
                                    case 0x30: program[ch] = v & 0x7F; break;
                                    case 0x31: drums[ch] = v == 0x80; break;
                                    case 0x32: octShift[ch] = v <= 4 ? v : -(v & 0x0F); break;
                                    case 0x37: volume[ch] = juce::jlimit (1, 127, v); break;
                                    default: break;
                                }
                            }
                        }
                        else if (ev == 0xFF)
                        {
                            if (p >= e) break;
                            if (*p == 0xF0) { ++p; if (p >= e) break; p += 1 + *p; }
                            else ++p;
                        }
                        else
                        {
                            int gate = 0;
                            if (! var (p, gate)) break;
                            const int ch = ev >> 6, oct = (ev >> 4) & 3, note = ev & 15;
                            const int midiIndex = juce::jlimit (0, 15, (juce::jmax (1, trackNo) - 1) * 4 + ch);
                            const int pitch = drums[ch] ? program[ch] : 36 + 12 * (oct + octShift[ch]) + note;
                            const int midiCh = drums[ch] ? 9 : (midiIndex >= 9 ? juce::jmin (15, midiIndex + 1) : midiIndex);
                            auto& tr = tracksByChannel[midiIndex];
                            if (tr < 0)
                            {
                                tr = b.addTrack();
                                if (! drums[ch]) b.program (tr, 0, midiCh, program[ch]);
                            }
                            b.note (tr, midiCh, t, t + juce::jmax (1, gate * tbG), pitch, volume[ch]);
                        }
                    }
                }
                q += 8 + sl;
            }
        }
        pos += 8 + len;
    }
    if (! b.hasNotes()) { error = unsupported ? T ("SMAF „Mobile Standard“ (MA-3/MA-5) wird nicht unterstützt") : T ("Keine Noten gefunden"); return false; }
    out.format = "SMAF";
    out.smf = b.build();
    return true;
}
} // namespace ma::fmt
