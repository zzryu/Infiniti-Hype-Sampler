#include "PluginEditor.h"

// ------------------------------------------------------------------ look and feel
InfinitiLookAndFeel::InfinitiLookAndFeel()
{
    const juce::Colour bg (0xff0a0a0a), panel (0xff131313), line (0xff3a3a3a), text (0xffe2e2e2);
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::TextButton::buttonColourId, panel);
    setColour (juce::TextButton::buttonOnColourId, panel);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, accent());
    setColour (juce::ComboBox::backgroundColourId, panel);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, text);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2a2a2a));
    setColour (juce::PopupMenu::highlightedTextColourId, accent());
    setColour (juce::Label::textColourId, juce::Colour (0xff9a9a9a));
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::highlightColourId, juce::Colour (0xff2f4a35));
}

void InfinitiLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    juce::Colour fill (0xff131313);
    if (down) fill = juce::Colour (0xff2c2c2c);
    else if (over) fill = juce::Colour (0xff1f1f1f);
    if (b.getToggleState()) fill = accent().withAlpha (0.18f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 3.f);
    g.setColour (b.getToggleState() ? accent().withAlpha (0.8f) : juce::Colour (0xff3a3a3a));
    g.drawRoundedRectangle (r, 3.f, 1.f);
}

juce::Font InfinitiLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return juce::Font (juce::jmin (15.f, (float) h * 0.5f), juce::Font::bold);
}

void InfinitiLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                            float start, float end, juce::Slider&)
{
    const auto b = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.f);
    const float rad = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = start + pos * (end - start);
    juce::Path track, value;
    track.addCentredArc (c.x, c.y, rad, rad, 0.f, start, end, true);
    value.addCentredArc (c.x, c.y, rad, rad, 0.f, start, angle, true);
    g.setColour (juce::Colour (0xff2a2a2a));
    g.strokePath (track, juce::PathStrokeType (3.f));
    g.setColour (accent());
    g.strokePath (value, juce::PathStrokeType (3.f));
    juce::Path ptr;
    ptr.addRectangle (-1.2f, -rad + 6.f, 2.4f, rad * 0.45f);
    g.setColour (juce::Colour (0xffe2e2e2));
    g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

// ------------------------------------------------------------------ editor
InfinitiEditor::InfinitiEditor (InfinitiProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&laf);
    setSize (760, 560);

    display.setMultiLine (true, true);
    display.setReturnKeyStartsNewLine (false);
    display.setInputRestrictions (140);
    display.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 26.f, juce::Font::plain));
    display.setColour (juce::TextEditor::backgroundColourId, juce::Colours::black);
    display.setColour (juce::TextEditor::textColourId, InfinitiLookAndFeel::accent());
    display.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff3a3a3a));
    display.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xff6a6a6a));
    display.setColour (juce::CaretComponent::caretColourId, InfinitiLookAndFeel::accent());
    display.setText (proc.getText(), false);
    display.addListener (this);
    addAndMakeVisible (display);

    for (auto* b : { &spaceBtn, &delBtn, &clearBtn, &langBtn, &speakBtn, &aliasBtn })
        addAndMakeVisible (b);
    addAndMakeVisible (gateBtn);
    gateBtn.setClickingTogglesState (true);

    spaceBtn.onClick = [this] { appendText (" "); };
    delBtn.onClick   = [this] { display.setText (display.getText().dropLastCharacters (1), true); };
    clearBtn.onClick = [this] { display.setText ({}, true); };
    langBtn.onClick  = [this] { ru = ! ru; langBtn.setButtonText (ru ? "RU" : "EN"); rebuildKeys(); };
    aliasBtn.onClick = [this] { appendText ("INFINITI HYPE MACHINE"); };
    speakBtn.onClick = [this]
    {
        proc.keyboardState.noteOn (1, 48, 0.9f);
        juce::Component::SafePointer<InfinitiEditor> sp (this);
        juce::Timer::callAfterDelay (150, [sp] { if (sp != nullptr) sp->proc.keyboardState.noteOff (1, 48, 0.f); });
    };

    setupKnob (speed, speedL, "SPEED", "x", 2);
    setupKnob (pitch, pitchL, "PITCH", " st", 1);
    setupKnob (formant, formantL, "FORMANT", "", 2);
    setupKnob (bits, bitsL, "BITS", "", 0);
    setupKnob (rate, rateL, "RATE", " Hz", 0);
    setupKnob (vol, volL, "VOLUME", "", 2);
    speedA = std::make_unique<SA> (proc.apvts, "speed", speed);
    pitchA = std::make_unique<SA> (proc.apvts, "pitch", pitch);
    formantA = std::make_unique<SA> (proc.apvts, "formant", formant);
    bitsA = std::make_unique<SA> (proc.apvts, "bits", bits);
    rateA = std::make_unique<SA> (proc.apvts, "rate", rate);
    volA = std::make_unique<SA> (proc.apvts, "volume", vol);

    modeBox.addItemList ({ "PHRASE", "WORD STEP" }, 1);
    addAndMakeVisible (modeBox);
    modeA = std::make_unique<CA> (proc.apvts, "mode", modeBox);
    gateA = std::make_unique<BA> (proc.apvts, "gate", gateBtn);

    keyboard.setAvailableRange (36, 84);
    keyboard.setKeyWidth (26.f);
    keyboard.setLowestVisibleKey (36);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setMidiChannel (1);
    keyboard.setWantsKeyboardFocus (false);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffd6d6d6));
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff050505));
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff3a3a3a));
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, InfinitiLookAndFeel::accent().withAlpha (0.25f));
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, InfinitiLookAndFeel::accent().withAlpha (0.6f));
    keyboard.setColour (juce::MidiKeyboardComponent::textLabelColourId, juce::Colour (0xff444444));
    addAndMakeVisible (keyboard);

    rebuildKeys();
}

InfinitiEditor::~InfinitiEditor()
{
    display.removeListener (this);
    setLookAndFeel (nullptr);
}

void InfinitiEditor::setupKnob (juce::Slider& s, juce::Label& l, const juce::String& name, const juce::String& suffix, int decimals)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
    s.setTextValueSuffix (suffix);
    s.setNumDecimalPlacesToDisplay (decimals);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    addAndMakeVisible (s);
    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::Font (11.f, juce::Font::bold));
    l.attachToComponent (&s, false);
    addAndMakeVisible (l);
}

void InfinitiEditor::appendText (const juce::String& s)
{
    auto t = display.getText();
    if (s.length() > 1 && t.isNotEmpty() && ! t.endsWithChar (' ')) t << " ";
    display.setText (t + s, true);
}

void InfinitiEditor::textEditorTextChanged (juce::TextEditor&) { proc.setText (display.getText()); }

void InfinitiEditor::rebuildKeys()
{
    keys.clear();
    juce::StringArray labels;
    if (ru)
    {
        for (int cp = 0x410; cp <= 0x415; ++cp) labels.add (juce::String::charToString ((juce::juce_wchar) cp));
        labels.add (juce::String::charToString ((juce::juce_wchar) 0x401));
        for (int cp = 0x416; cp <= 0x42F; ++cp) labels.add (juce::String::charToString ((juce::juce_wchar) cp));
    }
    else
        for (char c = 'A'; c <= 'Z'; ++c) labels.add (juce::String::charToString ((juce::juce_wchar) c));
    for (char c = '0'; c <= '9'; ++c) labels.add (juce::String::charToString ((juce::juce_wchar) c));

    for (auto& l : labels)
    {
        auto* b = keys.add (new juce::TextButton (l));
        b->onClick = [this, l] { appendText (l); };
        addAndMakeVisible (b);
    }
    resized();
}

void InfinitiEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0a0a0a));
    g.setColour (juce::Colour (0xffe2e2e2));
    g.setFont (juce::Font (15.f, juce::Font::bold));
    g.drawText ("INFINITI HYPE MACHINE", 20, 12, 400, 22, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff6a6a6a));
    g.setFont (juce::Font (12.f, juce::Font::plain));
    g.drawText ("VOICE  /  FORMANT SPEECH SYNTH  /  EN + RU", 340, 12, 400, 22, juce::Justification::centredRight);
    g.setColour (juce::Colour (0xff2a2a2a));
    g.drawHorizontalLine (38, 20.f, 740.f);
}

void InfinitiEditor::resized()
{
    const int mx = 20, w = getWidth() - 2 * mx;
    display.setBounds (mx, 48, w, 70);

    int y = 126, bh = 30, x = mx;
    auto place = [&] (juce::Component& c, int cw) { c.setBounds (x, y, cw, bh); x += cw + 6; };
    place (spaceBtn, 76); place (delBtn, 56); place (clearBtn, 66); place (langBtn, 50); place (speakBtn, 76);
    aliasBtn.setBounds (x, y, mx + w - x, bh);

    y = 166;
    const int cols = 11, gap = 4, kw = (w - gap * (cols - 1)) / cols, kh = 34;
    for (int i = 0; i < keys.size(); ++i)
        keys[i]->setBounds (mx + (i % cols) * (kw + gap), y + (i / cols) * (kh + gap), kw, kh);

    const int ky = 338, kn = 84, kx = mx;
    int k = 0;
    for (auto* s : { &speed, &pitch, &formant, &bits, &rate, &vol })
        s->setBounds (kx + k++ * (kn + 6), ky + 16, kn, 84);
    modeBox.setBounds (kx + 6 * (kn + 6) + 20, ky + 24, w - 6 * (kn + 6) - 20, 28);
    gateBtn.setBounds (modeBox.getX(), ky + 60, modeBox.getWidth(), 28);

    keyboard.setBounds (mx, 446, w, 100);
}
