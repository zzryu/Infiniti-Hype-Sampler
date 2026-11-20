#pragma once
#include "SamplerProcessor.h"

struct SamplerLookAndFeel : public juce::LookAndFeel_V4
{
    static juce::Colour accent() { return juce::Colour (0xff9be7a8); }
    SamplerLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

// waveform with slice markers: click = add marker, drag = move, right-click = delete,
// wheel = zoom, shift+wheel = pan, double-click = reset zoom. In scratch mode the mouse is the needle.
class WaveView : public juce::Component, private juce::Timer
{
public:
    explicit WaveView (SamplerProcessor&);
    void sampleChanged();
    void slicesChanged();
    void paint (juce::Graphics&) override;
    void resized() override { cacheDirty = true; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool scratchMode = false;
    std::function<void()> onSlicesEdited;

private:
    void timerCallback() override;
    double xToSample (float x) const;
    float sampleToX (double s) const;
    int nearestMarker (float x, float maxDist) const;
    void rebuildCache();

    SamplerProcessor& proc;
    std::shared_ptr<const ihs::SampleData> sample;
    std::vector<int> slices;
    double viewStart = 0, viewLen = 1;
    juce::Image cache;
    bool cacheDirty = true;
    int dragIdx = -1;
    double playPos = -1;
    int lastSlice = -1;
};

class SliceKeyboard : public juce::MidiKeyboardComponent
{
public:
    explicit SliceKeyboard (juce::MidiKeyboardState& s) : juce::MidiKeyboardComponent (s, horizontalKeyboard) {}
    int first = 36, count = 0;

protected:
    void drawWhiteNote (int n, juce::Graphics& g, juce::Rectangle<float> a, bool down, bool over, juce::Colour line, juce::Colour) override;
    void drawBlackNote (int n, juce::Graphics& g, juce::Rectangle<float> a, bool down, bool over, juce::Colour) override;
};

class SamplerEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit SamplerEditor (SamplerProcessor&);
    ~SamplerEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void loadFile (const juce::File&);
    void refreshInfo();
    void setupKnob (juce::Slider&, juce::Label&, const juce::String& name);

    SamplerProcessor& proc;
    SamplerLookAndFeel laf;
    WaveView wave;
    SliceKeyboard keyboard;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::TextButton loadBtn { "LOAD" }, gridBtn { "SLICE GRID" }, autoBtn { "SLICE AUTO" }, clearBtn { "CLEAR" }, scratchBtn { "SCRATCH" };
    juce::TextButton reverseBtn { "REVERSE" }, loopBtn { "LOOP" }, monoBtn { "MONO" }, keyBtn { "KEY PITCH" }, syncBtn { "SYNC" };
    juce::Label fileLabel, infoLabel;
    juce::ComboBox gridBox, modeBox, retrigBox;
    juce::Slider sens, gain, pitch, attack, release, srcbpm, chaos, bits, rate, drive, revchance, repstep, startnote;
    juce::Label gainL, pitchL, attackL, releaseL, srcbpmL, chaosL, bitsL, rateL, driveL, revchanceL, repstepL, startnoteL, sensL;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::vector<std::unique_ptr<SA>> sliderAtt;
    std::vector<std::unique_ptr<BA>> buttonAtt;
    std::vector<std::unique_ptr<CA>> comboAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerEditor)
};
