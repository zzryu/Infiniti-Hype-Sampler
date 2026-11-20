#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include "SamplerEngine.h"

class SamplerProcessor : public juce::AudioProcessor
{
public:
    SamplerProcessor();
    ~SamplerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Infiniti Hype Sampler"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // message-thread API used by the editor
    bool loadFile (const juce::File&, bool resetSlices = true);
    juce::File getLoadedFile() const { return loadedFile; }
    std::shared_ptr<const ihs::SampleData> getSample() const { return sampleMsg; }
    std::vector<int> getSlices() const { return slicesMsg ? *slicesMsg : std::vector<int> { 0 }; }
    void setSlices (std::vector<int> pts);
    void sliceGrid (int count);
    void sliceTransient (float sensitivity);
    void setScratch (bool active, double pos) { engine.setScratch (active, pos); }
    double getPlayhead() const { return engine.playheadPos(); }
    int getLastSlice() const { return engine.lastSlice(); }
    int getStartNote() const { return (int) apvts.getRawParameterValue ("startnote")->load(); }

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pushToEngine();

    juce::AudioFormatManager formats;
    ihs::Sampler engine;
    std::shared_ptr<const ihs::SampleData> sampleMsg;
    std::shared_ptr<const ihs::SlicePoints> slicesMsg;
    juce::File loadedFile;

    juce::SpinLock lock;
    std::shared_ptr<const ihs::SampleData> pendingSample;
    std::shared_ptr<const ihs::SlicePoints> pendingSlices;
    std::atomic<bool> dirty { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerProcessor)
};
