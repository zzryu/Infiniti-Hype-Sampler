#pragma once
#include "PluginProcessor.h"

// Strict black look: graphite panels, thin outlines, one pale-green accent.
struct InfinitiLookAndFeel : public juce::LookAndFeel_V4
{
    static juce::Colour accent() { return juce::Colour (0xff9be7a8); }

    InfinitiLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

class InfinitiEditor : public juce::AudioProcessorEditor, private juce::TextEditor::Listener
{
public:
    explicit InfinitiEditor (InfinitiProcessor&);
    ~InfinitiEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void textEditorTextChanged (juce::TextEditor&) override;
    void rebuildKeys();
    void appendText (const juce::String&);
    void setupKnob (juce::Slider&, juce::Label&, const juce::String& name, const juce::String& suffix, int decimals);

    InfinitiProcessor& proc;
    InfinitiLookAndFeel laf;
    bool ru = false;

    juce::TextEditor display;
    juce::OwnedArray<juce::TextButton> keys;
    juce::TextButton spaceBtn { "SPACE" }, delBtn { "DEL" }, clearBtn { "CLEAR" }, langBtn { "EN" },
                     speakBtn { "SPEAK" }, aliasBtn { "INFINITI HYPE MACHINE" }, gateBtn { "GATE" };
    juce::ComboBox modeBox;
    juce::Slider speed, pitch, formant, bits, rate, vol;
    juce::Label speedL, pitchL, formantL, bitsL, rateL, volL;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SA> speedA, pitchA, formantA, bitsA, rateA, volA;
    std::unique_ptr<BA> gateA;
    std::unique_ptr<CA> modeA;

    juce::MidiKeyboardComponent keyboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InfinitiEditor)
};
