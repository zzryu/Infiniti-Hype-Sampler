#include "SamplerEngine.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace ihs
{
SlicePoints gridSlices (int len, int count)
{
    SlicePoints p;
    if (len <= 0 || count < 1) return { 0 };
    count = std::min (count, 64);
    for (int i = 0; i < count; ++i) p.push_back ((int) ((long long) len * i / count));
    return p;
}

SlicePoints transientSlices (const SampleData& s, float sens, int maxSlices)
{
    SlicePoints out { 0 };
    const int len = s.length();
    if (len < 8192) return out;
    const int hop = 256, nh = len / hop;
    std::vector<float> env ((size_t) nh, 0.f);
    for (int h = 0; h < nh; ++h)
    {
        double acc = 0;
        const int b = h * hop;
        for (int i = std::max (1, b); i < b + hop; ++i)
            acc += std::fabs (0.5f * (s.l[(size_t) i] + s.r[(size_t) i]) - 0.5f * (s.l[(size_t) i - 1] + s.r[(size_t) i - 1]));
        env[(size_t) h] = (float) (acc / hop);
    }
    const float f = 1.15f + (1.f - std::min (1.f, std::max (0.f, sens))) * 3.5f;
    const int minGap = std::max (2, (int) (0.06 * s.sampleRate / hop));
    struct Cand { int pos; float strength; };
    std::vector<Cand> c;
    int last = -minGap;
    for (int h = 24; h < nh - 1; ++h)
    {
        double avg = 0;
        for (int k = h - 24; k < h; ++k) avg += env[(size_t) k];
        avg /= 24.0;
        const float thr = (float) (f * avg) + 0.003f;
        if (env[(size_t) h] > thr && env[(size_t) h - 1] <= thr && h - last >= minGap)
        {
            const int p = std::max (0, (h - 1) * hop);
            int best = p;
            for (int i = p; i < std::min (len - 1, p + 2 * hop); ++i)
            {
                const float a = 0.5f * (s.l[(size_t) i] + s.r[(size_t) i]);
                const float b2 = 0.5f * (s.l[(size_t) i + 1] + s.r[(size_t) i + 1]);
                if ((a <= 0 && b2 > 0) || (a >= 0 && b2 < 0)) { best = i; break; }
            }
            c.push_back ({ best, env[(size_t) h] / (float) (avg + 1e-6) });
            last = h;
        }
    }
    if ((int) c.size() > maxSlices - 1)
    {
        std::sort (c.begin(), c.end(), [] (const Cand& a, const Cand& b) { return a.strength > b.strength; });
        c.resize ((size_t) maxSlices - 1);
    }
    std::sort (c.begin(), c.end(), [] (const Cand& a, const Cand& b) { return a.pos < b.pos; });
    for (auto& x : c) if (x.pos > hop && x.pos > out.back() + hop) out.push_back (x.pos);
    return out;
}

struct Sampler::Impl
{
    struct Voice
    {
        bool active = false, held = false, releasing = false, choking = false, done = false, rev = false;
        int note = 0, start = 0, end = 0, retrigCtr = 0, repeat = 0;
        double pos = 0;
        float vel = 1.f, env = 0.f, keyMul = 1.f, repMul = 1.f;
        uint64_t age = 0;
    };

    double sr = 44100.0;
    std::shared_ptr<const SampleData> smp;
    std::shared_ptr<const SlicePoints> pts;
    EngineParams p;
    Voice v[16];
    uint64_t counter = 0;
    uint32_t rng = 2463534242u;
    std::atomic<double> playPos { -1.0 }, scratchTarget { 0.0 };
    std::atomic<int> lastSliceIdx { -1 };
    std::atomic<bool> scratchActive { false };
    double scratchPos = 0, scratchAmp = 0, scratchGain = 0;
    bool scratchRunning = false;
    float holdL = 0, holdR = 0, holdAcc = 0;

    float rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng & 0xFFFFFF) / 16777216.f; }

    bool gateLike() const { return p.gateMode || p.loop || p.retrigDiv > 0; }

    void read (double pos, float& l, float& r) const
    {
        const int len = smp->length();
        int i0 = (int) pos;
        if (i0 < 0) i0 = 0;
        if (i0 > len - 2) i0 = len - 2;
        float fr = (float) (pos - i0);
        fr = std::min (1.f, std::max (0.f, fr));
        l = smp->l[(size_t) i0] + (smp->l[(size_t) i0 + 1] - smp->l[(size_t) i0]) * fr;
        r = smp->r[(size_t) i0] + (smp->r[(size_t) i0 + 1] - smp->r[(size_t) i0]) * fr;
    }
};

Sampler::Sampler() : impl (new Impl) {}
Sampler::~Sampler() = default;
void Sampler::prepare (double sr) { impl->sr = sr; allOff(); }
void Sampler::setSample (std::shared_ptr<const SampleData> s) { impl->smp = std::move (s); for (auto& v : impl->v) v.active = false; }
void Sampler::setSlices (std::shared_ptr<const SlicePoints> p) { impl->pts = std::move (p); }
void Sampler::setParams (const EngineParams& p) { impl->p = p; }
void Sampler::setScratch (bool a, double t) { impl->scratchTarget = t; impl->scratchActive = a; }
double Sampler::playheadPos() const { return impl->playPos.load(); }
int Sampler::lastSlice() const { return impl->lastSliceIdx.load(); }

void Sampler::allOff()
{
    for (auto& v : impl->v) if (v.active) { v.held = false; v.releasing = true; }
}

void Sampler::noteOn (int note, float vel)
{
    auto& s = *impl;
    if (! s.smp || ! s.pts || s.pts->empty() || s.smp->length() < 4) return;
    const int count = (int) s.pts->size(), len = s.smp->length();
    int idx = note - s.p.startNote;
    if (idx < 0 || idx >= count) return;
    if (s.p.chaos > 0.f && s.rnd() < s.p.chaos) idx = std::min (count - 1, (int) (s.rnd() * count));
    const int st = (*s.pts)[(size_t) idx];
    const int en = idx + 1 < count ? (*s.pts)[(size_t) idx + 1] : len;
    if (en - st < 2) return;

    if (s.p.mono) for (auto& o : s.v) if (o.active) o.choking = true;

    Impl::Voice* use = nullptr;
    for (auto& o : s.v) if (! o.active) { use = &o; break; }
    if (! use) { use = &s.v[0]; for (auto& o : s.v) if (o.age < use->age) use = &o; }

    Impl::Voice nv;
    nv.active = true; nv.held = true; nv.note = note; nv.start = st; nv.end = en; nv.vel = vel;
    nv.rev = s.p.reverse != (s.rnd() < s.p.revChance);
    nv.pos = nv.rev ? en - 1 : st;
    nv.keyMul = s.p.keyPitch ? std::pow (2.f, (float) (note - 60) / 12.f) : 1.f;
    nv.age = ++s.counter;
    *use = nv;
    s.lastSliceIdx = idx;
}

void Sampler::noteOff (int note)
{
    auto& s = *impl;
    if (! s.gateLike()) return;
    for (auto& v : s.v)
        if (v.active && v.held && v.note == note) { v.held = false; v.releasing = true; }
}

void Sampler::process (float* L, float* R, int n, double hostBpm)
{
    auto& s = *impl;
    for (int i = 0; i < n; ++i) { L[i] = 0.f; R[i] = 0.f; }
    if (! s.smp || s.smp->length() < 4) { s.playPos = -1.0; return; }

    const auto& S = *s.smp;
    const double sr = s.sr;
    const double bpm = hostBpm > 1.0 ? hostBpm : 120.0;
    const double syncMul = s.p.sync ? bpm / std::max (20.f, s.p.srcBpm) : 1.0;
    const double rateBase = S.sampleRate / sr * std::pow (2.0, s.p.semis / 12.0) * syncMul;
    const int retrigSamples = s.p.retrigDiv > 0 ? std::max (32, (int) (sr * 60.0 / bpm * 4.0 / s.p.retrigDiv)) : 0;
    const float atkStep = 1.f / (float) std::max (sr * 0.002, sr * 0.001 * s.p.attackMs);
    const float relStep = 1.f / (float) std::max (sr * 0.002, sr * 0.001 * s.p.releaseMs);
    const float chokeStep = 1.f / (float) (0.003 * sr);
    const bool loop = s.p.loop;

    double newest = -1.0;
    uint64_t newestAge = 0;
    for (auto& v : s.v)
    {
        if (! v.active) continue;
        for (int i = 0; i < n; ++i)
        {
            if (retrigSamples > 0 && v.held && ++v.retrigCtr >= retrigSamples)
            {
                v.retrigCtr = 0; ++v.repeat; v.done = false;
                v.repMul = std::min (8.f, std::max (0.125f, std::pow (2.f, (float) v.repeat * s.p.retrigStep / 12.f)));
                v.pos = v.rev ? v.end - 1 : v.start;
            }
            if (v.choking) { v.env -= chokeStep; if (v.env <= 0.f) { v.active = false; break; } }
            else if (v.releasing) { v.env -= relStep; if (v.env <= 0.f) { v.active = false; break; } }
            else if (v.env < 1.f) v.env = std::min (1.f, v.env + atkStep);

            if (v.done)
            {
                if (v.releasing) { v.active = false; break; }
                continue;
            }

            float l, r;
            s.read (v.pos, l, r);
            const double dist = v.rev ? v.pos - v.start : v.end - v.pos;
            const float fade = (float) std::min (1.0, std::max (0.0, dist / 64.0));
            const float g = v.env * fade * v.vel;
            L[i] += l * g;
            R[i] += r * g;

            const double step = rateBase * v.keyMul * v.repMul;
            v.pos += v.rev ? -step : step;
            if (v.rev ? v.pos <= v.start : v.pos >= v.end)
            {
                if (loop) v.pos = v.rev ? v.end - 1 : v.start;
                else if (retrigSamples > 0 && v.held) v.done = true;
                else { v.active = false; break; }
            }
        }
        if (v.active && (newest < 0 || v.age >= newestAge)) { newest = v.pos; newestAge = v.age; }
    }

    // scratch: the needle follows the mouse, sound depends on needle speed
    const bool act = s.scratchActive.load();
    const double tgt = s.scratchTarget.load();
    if (act && ! s.scratchRunning) { s.scratchRunning = true; s.scratchPos = tgt; s.scratchGain = 0; }
    if (s.scratchRunning)
    {
        const double alpha = 1.0 - std::exp (-1.0 / (0.012 * sr));
        const double ampC = 1.0 - std::exp (-1.0 / (0.003 * sr));
        for (int i = 0; i < n; ++i)
        {
            const double old = s.scratchPos;
            s.scratchPos += (tgt - s.scratchPos) * alpha;
            s.scratchPos = std::min ((double) S.length() - 2.0, std::max (0.0, s.scratchPos));
            s.scratchAmp += ((act ? 1.0 : 0.0) - s.scratchAmp) * ampC;
            const float gt = (float) std::min (1.0, std::fabs (s.scratchPos - old) * 3.0);
            s.scratchGain += (gt - s.scratchGain) * 0.01f;
            float l, r;
            s.read (s.scratchPos, l, r);
            const float g = (float) s.scratchAmp * s.scratchGain;
            L[i] += l * g; R[i] += r * g;
            if (! act && s.scratchAmp < 0.001) { s.scratchRunning = false; break; }
        }
        newest = s.scratchPos;
    }
    s.playPos = newest;

    // master: drive, crush, gain
    const float amt = 1.f + s.p.drive * 8.f, comp = 1.f / std::sqrt (amt);
    const bool crushOn = s.p.bits < 15.5f || s.p.rate < sr * 0.99f;
    const float holdPeriod = std::max (1.f, (float) (sr / std::max (1000.f, s.p.rate)));
    const float levels = std::pow (2.f, std::max (2.f, s.p.bits) - 1.f);
    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];
        if (s.p.drive > 0.f) { l = std::tanh (l * amt) * comp; r = std::tanh (r * amt) * comp; }
        if (crushOn)
        {
            s.holdAcc += 1.f;
            if (s.holdAcc >= holdPeriod) { s.holdAcc -= holdPeriod; s.holdL = l; s.holdR = r; }
            l = std::round (s.holdL * levels) / levels;
            r = std::round (s.holdR * levels) / levels;
        }
        L[i] = std::min (1.f, std::max (-1.f, l * s.p.gain));
        R[i] = std::min (1.f, std::max (-1.f, r * s.p.gain));
    }
}
}
