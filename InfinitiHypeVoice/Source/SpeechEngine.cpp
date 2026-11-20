#include "SpeechEngine.h"
#include <algorithm>
#include <cmath>

namespace ihm
{
namespace
{
constexpr float kPi = 3.14159265358979f;

struct Reso
{
    float a = 1, b = 0, c = 0, z1 = 0, z2 = 0;
    void set (float f, float bw, float fs)
    {
        f  = std::min (std::max (f, 60.f), 0.45f * fs);
        bw = std::min (std::max (bw, 30.f), 0.4f * fs);
        c = -std::exp (-2.f * kPi * bw / fs);
        b = 2.f * std::exp (-kPi * bw / fs) * std::cos (2.f * kPi * f / fs);
        a = 1.f - b - c;
    }
    float tick (float x) { float y = a * x + b * z1 + c * z2; z2 = z1; z1 = y; return y; }
    void reset() { z1 = z2 = 0; }
};

float glot (float p)
{
    const float tp = 0.6f;
    return p < tp ? 0.5f * (1.f - std::cos (kPi * p / tp))
                  : std::cos (kPi * (p - tp) / (2.f * (1.f - tp)));
}
float lerp (float a, float b, float t) { return a + (b - a) * t; }
}

struct Voice::Impl
{
    double sr = 44100.0;
    Params p;
    std::shared_ptr<const Utterance> utt;
    bool running = false, releasing = false;
    int note = -1;
    float vel = 1.f;
    size_t segBegin = 0, segEnd = 0, segIdx = 0;
    double segPos = 0, segLen = 1;
    Segment cur {};
    float f1 = 500, f2 = 1500, f3 = 2500, vAmp = 0, nAmp = 0, aAmp = 0, nf = 3000, nbw = 2000;
    float tnf = 3000, tnbw = 2000;
    float phase = 0, prevG = 0, env = 0, f0base = 130.f;
    Reso r1, r2, r3, r4, nr;
    uint32_t rng = 22222u;
    float holdAcc = 0, held = 0;
    int ctr = 0;

    float white()
    {
        rng = rng * 1664525u + 1013904223u;
        return (float) (int32_t) rng * (1.f / 2147483648.f);
    }

    void loadSeg (size_t idx)
    {
        cur = utt->segs[idx];
        segLen = std::max (1.0, (double) cur.ms * 0.001 * sr / std::max (0.25f, p.speed));
        segPos = 0;
        if (cur.noise > 0.f && ! cur.aspirate) { tnf = cur.nf; tnbw = cur.nbw; }
    }
};

Voice::Voice() : impl (new Impl) {}
Voice::~Voice() = default;
void Voice::prepare (double sr) { impl->sr = sr; impl->running = false; impl->env = 0; }
void Voice::setParams (const Params& p) { impl->p = p; }
void Voice::setUtterance (std::shared_ptr<const Utterance> u) { impl->utt = std::move (u); }
bool Voice::isActive() const { return impl->running; }

void Voice::noteOn (int n, float v, int wordIdx)
{
    auto& s = *impl;
    if (! s.utt || s.utt->segs.empty()) return;
    size_t b = 0, e = s.utt->segs.size();
    if (wordIdx >= 0 && wordIdx < s.utt->numWords())
    {
        b = s.utt->wordStart[(size_t) wordIdx];
        e = s.utt->wordStart[(size_t) wordIdx + 1];
    }
    if (b >= e) return;
    s.segBegin = b; s.segEnd = e; s.segIdx = b;
    s.note = n; s.vel = v;
    s.loadSeg (b);
    s.nf = s.tnf; s.nbw = s.tnbw;
    if (! s.running || s.env <= 0.001f)
    {
        s.f1 = s.cur.f1; s.f2 = s.cur.f2; s.f3 = s.cur.f3;
        s.vAmp = s.nAmp = s.aAmp = 0; s.env = 0;
        s.r1.reset(); s.r2.reset(); s.r3.reset(); s.r4.reset(); s.nr.reset();
    }
    s.running = true; s.releasing = false;
    s.phase = 0; s.prevG = 0;
    s.f0base = 440.f * std::pow (2.f, ((float) n - 69.f + s.p.semis) / 12.f);
}

void Voice::noteOff (int n)
{
    auto& s = *impl;
    if (s.running && s.p.gate && n == s.note) s.releasing = true;
}

void Voice::allOff() { if (impl->running) impl->releasing = true; }

void Voice::process (float* out, int num)
{
    auto& s = *impl;
    const float fs = (float) s.sr;
    const float cF = 1.f - std::exp (-1.f / (0.012f * fs));
    const float cV = 1.f - std::exp (-1.f / (0.004f * fs));
    const float cN = 1.f - std::exp (-1.f / (0.0015f * fs));
    const float attack = 1.f / (0.003f * fs), rel = 1.f / (0.03f * fs);
    const float holdPeriod = std::max (1.f, fs / std::max (1000.f, s.p.rate));
    const float levels = std::pow (2.f, std::max (2.f, s.p.bits) - 1.f);
    const float fm = s.p.formant;

    for (int i = 0; i < num; ++i)
    {
        if (! s.running) { out[i] = 0.f; continue; }

        if (! s.releasing && s.segPos >= s.segLen)
        {
            ++s.segIdx;
            if (s.segIdx >= s.segEnd) s.releasing = true;
            else s.loadSeg (s.segIdx);
        }

        float prog = 0.f;
        if (! s.releasing)
        {
            const float t = (float) (s.segPos / s.segLen);
            s.f1 += (lerp (s.cur.f1, s.cur.e1, t) * fm - s.f1) * cF;
            s.f2 += (lerp (s.cur.f2, s.cur.e2, t) * fm - s.f2) * cF;
            s.f3 += (lerp (s.cur.f3, s.cur.e3, t) * fm - s.f3) * cF;
            s.vAmp += (s.cur.voice - s.vAmp) * cV;
            s.nAmp += ((s.cur.aspirate ? 0.f : s.cur.noise) - s.nAmp) * cN;
            s.aAmp += ((s.cur.aspirate ? s.cur.noise : 0.f) - s.aAmp) * cN;
            s.segPos += 1.0;
            prog = (float) (s.segIdx - s.segBegin) / (float) std::max<size_t> (1, s.segEnd - s.segBegin);
        }
        else
        {
            s.vAmp += (0.f - s.vAmp) * cV; s.nAmp += (0.f - s.nAmp) * cN; s.aAmp += (0.f - s.aAmp) * cN;
            prog = 1.f;
        }
        s.nf  += (s.tnf  - s.nf)  * cF;
        s.nbw += (s.tnbw - s.nbw) * cF;

        if ((s.ctr++ & 15) == 0)
        {
            s.r1.set (s.f1, 70.f, fs);  s.r2.set (s.f2, 100.f, fs);
            s.r3.set (s.f3, 140.f, fs); s.r4.set (3500.f * fm, 220.f, fs);
            s.nr.set (s.nf, s.nbw, fs);
        }

        const float f0 = s.f0base * (1.f - 0.07f * prog);
        const float inc = f0 / fs;
        s.phase += inc;
        if (s.phase >= 1.f) s.phase -= 1.f;
        const float g = glot (s.phase);
        const float d = (g - s.prevG) / inc;
        s.prevG = g;

        float src = d * 0.25f * s.vAmp + s.white() * 0.02f * s.vAmp + s.white() * 0.5f * s.aAmp;
        float y = s.r4.tick (s.r3.tick (s.r2.tick (s.r1.tick (src))));
        float nz = s.nr.tick (s.white()) * (s.nbw / std::max (500.f, s.nf));
        float x = y * 0.33f + nz * s.nAmp * 0.8f;

        if (s.releasing)
        {
            s.env -= rel;
            if (s.env <= 0.f) { s.env = 0.f; s.running = false; out[i] = 0.f; continue; }
        }
        else if (s.env < 1.f) s.env = std::min (1.f, s.env + attack);

        x *= s.env * s.vel * s.p.volume;
        x = std::tanh (1.2f * x);

        s.holdAcc += 1.f;
        if (s.holdAcc >= holdPeriod) { s.holdAcc -= holdPeriod; s.held = x; }
        out[i] = std::round (s.held * levels) / levels;
    }
}
} // namespace ihm
