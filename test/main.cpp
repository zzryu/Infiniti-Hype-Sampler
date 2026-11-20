#include "../Source/SpeechEngine.h"
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>

static void wav (const char* path, const std::vector<float>& d, int sr)
{
    FILE* f = fopen (path, "wb");
    uint32_t dataBytes = (uint32_t) d.size() * 2, riff = 36 + dataBytes, fmt = 16, rate = (uint32_t) sr, br = rate * 2;
    uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
    fwrite ("RIFF", 1, 4, f); fwrite (&riff, 4, 1, f); fwrite ("WAVEfmt ", 1, 8, f); fwrite (&fmt, 4, 1, f);
    fwrite (&pcm, 2, 1, f); fwrite (&ch, 2, 1, f); fwrite (&rate, 4, 1, f); fwrite (&br, 4, 1, f);
    fwrite (&ba, 2, 1, f); fwrite (&bits, 2, 1, f); fwrite ("data", 1, 4, f); fwrite (&dataBytes, 4, 1, f);
    for (float v : d) { int16_t s = (int16_t) std::lround (std::max (-1.f, std::min (1.f, v)) * 32767.f); fwrite (&s, 2, 1, f); }
    fclose (f);
}

int main (int argc, char** argv)
{
    std::string text = argc > 1 ? argv[1] : "infiniti hype machine";
    const char* outPath = argc > 2 ? argv[2] : "out.wav";
    std::string dbg;
    auto u = ihm::buildUtterance (text, &dbg);
    printf ("text: %s\nphonemes: %s\nsegments: %zu words: %d\n", text.c_str(), dbg.c_str(), u->segs.size(), u->numWords());
    double total = 0; for (auto& s : u->segs) total += s.ms;
    printf ("nominal length: %.0f ms\n", total);

    const int sr = 44100;
    ihm::Voice v; v.prepare (sr);
    ihm::Params p; v.setParams (p); v.setUtterance (u); v.noteOn (48, 0.9f, -1);
    std::vector<float> buf ((size_t) sr * 5);
    for (size_t pos = 0; pos < buf.size(); pos += 512)
        v.process (buf.data() + pos, (int) std::min<size_t> (512, buf.size() - pos));
    float peak = 0; bool bad = false; size_t last = 0;
    for (size_t i = 0; i < buf.size(); ++i) { if (!std::isfinite (buf[i])) bad = true; peak = std::max (peak, std::fabs (buf[i])); if (std::fabs (buf[i]) > 1e-4f) last = i; }
    printf ("peak %.3f nan %d audible until %.0f ms active-after %d\n", peak, (int) bad, last * 1000.0 / sr, (int) v.isActive());
    printf ("windowed RMS (50ms):");
    for (size_t w = 0; w * 2205 < last; ++w) { double e = 0; for (size_t i = w * 2205; i < (w + 1) * 2205 && i < buf.size(); ++i) e += buf[i] * buf[i]; printf (" %.2f", std::sqrt (e / 2205)); }
    printf ("\n");
    buf.resize (last + 2000);
    wav (outPath, buf, sr);
    return 0;
}
