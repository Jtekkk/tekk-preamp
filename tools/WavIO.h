#pragma once
#include <vector>
#include <string>
#include <cstdio>
#include <cstdint>
#include <cstring>

// ============================================================================
//  WavIO : tiny dependency-free WAV read/write for the capture tool.
//  Reads channel 0 of PCM16/24/32 or float32 WAV into mono floats; writes
//  mono float32. Enough to round-trip a DAW capture; not a general codec.
// ============================================================================
namespace wav
{
struct Audio { int fs = 48000; std::vector<float> mono; };

inline uint32_t rd32 (const unsigned char* p)
{ return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
inline uint16_t rd16 (const unsigned char* p)
{ return (uint16_t) ((uint32_t) p[0] | ((uint32_t) p[1] << 8)); }

inline bool read (const std::string& path, Audio& out)
{
    FILE* f = std::fopen (path.c_str(), "rb");
    if (! f) return false;
    std::fseek (f, 0, SEEK_END); long sz = std::ftell (f); std::fseek (f, 0, SEEK_SET);
    if (sz < 44) { std::fclose (f); return false; }
    std::vector<unsigned char> b ((size_t) sz);
    bool ok = std::fread (b.data(), 1, (size_t) sz, f) == (size_t) sz;
    std::fclose (f);
    if (! ok) return false;
    if (std::memcmp (b.data(), "RIFF", 4) || std::memcmp (b.data() + 8, "WAVE", 4)) return false;

    uint16_t fmt = 1, ch = 1, bits = 16; uint32_t rate = 48000;
    const unsigned char* data = nullptr; uint32_t dataLen = 0;
    size_t pos = 12;
    while (pos + 8 <= (size_t) sz)
    {
        const unsigned char* id = b.data() + pos;
        uint32_t len = rd32 (b.data() + pos + 4);
        const unsigned char* body = b.data() + pos + 8;
        if (! std::memcmp (id, "fmt ", 4) && len >= 16)
        { fmt = rd16 (body); ch = rd16 (body + 2); rate = rd32 (body + 4); bits = rd16 (body + 14); }
        else if (! std::memcmp (id, "data", 4))
        { data = body; dataLen = len; }
        pos += 8 + len + (len & 1);             // chunks are word-aligned
    }
    if (! data || ch == 0) return false;

    const int bytes = bits / 8;
    const uint32_t frames = dataLen / (uint32_t) (bytes * ch);
    out.fs = (int) rate;
    out.mono.resize (frames);
    for (uint32_t i = 0; i < frames; ++i)
    {
        const unsigned char* s = data + (size_t) i * bytes * ch;   // channel 0
        float v = 0.0f;
        if      (fmt == 3 && bits == 32) { std::memcpy (&v, s, 4); }
        else if (fmt == 1 && bits == 16) { v = (int16_t) rd16 (s) / 32768.0f; }
        else if (fmt == 1 && bits == 24)
        {
            int32_t iv = s[0] | (s[1] << 8) | (s[2] << 16);
            if (iv & 0x800000) iv |= ~0xFFFFFF;             // sign-extend 24->32
            v = iv / 8388608.0f;
        }
        else if (fmt == 1 && bits == 32) { v = (int32_t) rd32 (s) / 2147483648.0f; }
        else return false;
        out.mono[i] = v;
    }
    return true;
}

inline bool write (const std::string& path, const Audio& a)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return false;
    const uint32_t n = (uint32_t) a.mono.size();
    const uint16_t ch = 1, bits = 32, fmt = 3;
    const uint32_t rate = (uint32_t) a.fs, dataLen = n * 4;
    const uint32_t byteRate = rate * ch * bits / 8;
    const uint16_t blockAlign = ch * bits / 8;
    auto w32 = [&] (uint32_t v) { unsigned char p[4] { (unsigned char) v, (unsigned char)(v>>8),
                                                       (unsigned char)(v>>16), (unsigned char)(v>>24) };
                                  std::fwrite (p, 1, 4, f); };
    auto w16 = [&] (uint16_t v) { unsigned char p[2] { (unsigned char) v, (unsigned char)(v>>8) };
                                  std::fwrite (p, 1, 2, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataLen); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); w32 (16); w16 (fmt); w16 (ch); w32 (rate);
    w32 (byteRate); w16 (blockAlign); w16 (bits);
    std::fwrite ("data", 1, 4, f); w32 (dataLen);
    std::fwrite (a.mono.data(), 4, n, f);
    std::fclose (f);
    return true;
}
} // namespace wav
