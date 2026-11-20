#pragma once
#include <memory>
#include <string>
#include <vector>

// Own formant speech engine (no third-party speech code). Pure C++, no JUCE dependency.
namespace ihm
{
struct Segment
{
    float f1, f2, f3;            // formant targets at segment start (Hz)
    float e1, e2, e3;            // formant targets at segment end (diphthongs glide)
    float voice, noise;          // voiced / noise level 0..1
    float nf, nbw;               // noise centre / bandwidth (Hz)
    bool  aspirate;              // noise goes through the vocal-tract filter (h, plosive release)
    float ms;                    // nominal duration
};

struct Utterance
{
    std::vector<Segment> segs;
    std::vector<size_t> wordStart;   // numWords + 1 entries, last = segs.size()
    int numWords() const { return wordStart.empty() ? 0 : (int) wordStart.size() - 1; }
};

// utf8 text -> utterance. English + Russian, digits, and {ARPAbet tokens} for manual phonetics.
std::shared_ptr<const Utterance> buildUtterance (const std::string& utf8, std::string* debug = nullptr);

struct Params
{
    float speed = 1.f, semis = 0.f, formant = 1.f, bits = 8.f, rate = 11025.f, volume = 0.8f;
    bool gate = false;
};

class Voice
{
public:
    Voice();
    ~Voice();
    void prepare (double sampleRate);
    void setParams (const Params& p);
    void setUtterance (std::shared_ptr<const Utterance> u);
    void noteOn (int midiNote, float velocity, int wordIndex /* -1 = whole phrase */);
    void noteOff (int midiNote);
    void allOff();
    void process (float* out, int numSamples);
    bool isActive() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
