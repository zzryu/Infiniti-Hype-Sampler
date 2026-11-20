#pragma once
#include <memory>
#include <vector>

// Slicing sampler engine (pure C++, no JUCE dependency).
namespace ihs
{
struct SampleData
{
    std::vector<float> l, r;
    double sampleRate = 44100.0;
    int length() const { return (int) l.size(); }
};

using SlicePoints = std::vector<int>;   // sorted slice start positions, first is always 0

SlicePoints gridSlices (int length, int count);
SlicePoints transientSlices (const SampleData&, float sensitivity /* 0..1 */, int maxSlices = 64);

struct EngineParams
{
    float gain = 0.8f, semis = 0.f;
    bool keyPitch = false, reverse = false, loop = false, mono = false, gateMode = false, sync = false;
    float revChance = 0.f, chaos = 0.f;
    float attackMs = 0.f, releaseMs = 30.f;
    int retrigDiv = 0;          // 0 = off, otherwise 1/4, 1/8, 1/16 ...
    float retrigStep = 0.f;     // semitones added per repeat (stutter riser)
    float srcBpm = 170.f;
    float bits = 16.f, rate = 44100.f, drive = 0.f;
    int startNote = 36;
};

class Sampler
{
public:
    Sampler();
    ~Sampler();
    void prepare (double sampleRate);
    void setSample (std::shared_ptr<const SampleData>);
    void setSlices (std::shared_ptr<const SlicePoints>);
    void setParams (const EngineParams&);
    void noteOn (int note, float velocity);
    void noteOff (int note);
    void allOff();
    void setScratch (bool active, double targetSamplePos);   // thread-safe
    void process (float* left, float* right, int numSamples, double hostBpm);
    double playheadPos() const;   // -1 when silent
    int lastSlice() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
