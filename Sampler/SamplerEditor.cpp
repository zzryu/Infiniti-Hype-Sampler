#include "SamplerEditor.h"

// ------------------------------------------------------------------ look and feel
SamplerLookAndFeel::SamplerLookAndFeel()
{
    const juce::Colour panel (0xff131313), line (0xff3a3a3a), text (0xffe2e2e2);
    setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff0a0a0a));
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
    setColour (juce::Slider::trackColourId, accent());
    setColour (juce::Slider::backgroundColourId, juce::Colour (0xff2a2a2a));
    setColour (juce::Slider::thumbColourId, text);
}

void SamplerLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
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

juce::Font SamplerLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return juce::Font (juce::jmin (14.f, (float) h * 0.5f), juce::Font::bold);
}

void SamplerLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
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

// ------------------------------------------------------------------ waveform view
WaveView::WaveView (SamplerProcessor& p) : proc (p) { startTimerHz (30); }

void WaveView::sampleChanged()
{
    sample = proc.getSample();
    viewStart = 0;
    viewLen = sample ? (double) sample->length() : 1.0;
    slices = proc.getSlices();
    cacheDirty = true;
    repaint();
}

void WaveView::slicesChanged() { slices = proc.getSlices(); repaint(); }

double WaveView::xToSample (float x) const { return viewStart + (double) x / juce::jmax (1, getWidth()) * viewLen; }
float WaveView::sampleToX (double s) const { return (float) ((s - viewStart) / viewLen * getWidth()); }

int WaveView::nearestMarker (float x, float maxDist) const
{
    int best = -1;
    float bd = maxDist;
    for (size_t i = 1; i < slices.size(); ++i)
    {
        const float d = std::abs (sampleToX (slices[i]) - x);
        if (d <= bd) { bd = d; best = (int) i; }
    }
    return best;
}

void WaveView::rebuildCache()
{
    const int w = getWidth(), h = getHeight();
    cache = juce::Image (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
    cacheDirty = false;
    if (! sample || sample->length() < 2) return;
    juce::Graphics g (cache);
    g.setColour (juce::Colour (0xff6fae7c));
    const int len = sample->length();
    const float mid = h * 0.5f;
    for (int x = 0; x < w; ++x)
    {
        const double s0 = viewStart + (double) x / w * viewLen, s1 = viewStart + (double) (x + 1) / w * viewLen;
        const int a = juce::jlimit (0, len - 1, (int) s0), b = juce::jlimit (a + 1, len, (int) std::ceil (s1));
        const int stride = juce::jmax (1, (b - a) / 256);
        float mn = 1.f, mx = -1.f;
        for (int i = a; i < b; i += stride)
        {
            const float v = 0.5f * (sample->l[(size_t) i] + sample->r[(size_t) i]);
            mn = juce::jmin (mn, v); mx = juce::jmax (mx, v);
        }
        if (mn > mx) continue;
        g.fillRect ((float) x, mid - mx * mid * 0.95f, 1.f, juce::jmax (1.f, (mx - mn) * mid * 0.95f));
    }
}

void WaveView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    g.setColour (juce::Colour (0xff3a3a3a));
    g.drawRect (getLocalBounds(), 1);

    if (! sample)
    {
        g.setColour (juce::Colour (0xff6a6a6a));
        g.setFont (juce::Font (16.f, juce::Font::bold));
        g.drawText ("DROP AN MP3 / WAV HERE  -  OR PRESS LOAD", getLocalBounds(), juce::Justification::centred);
        return;
    }

    if (cacheDirty || cache.getWidth() != getWidth() || cache.getHeight() != getHeight()) rebuildCache();

    // highlight the last played slice
    if (lastSlice >= 0 && lastSlice < (int) slices.size())
    {
        const double s0 = slices[(size_t) lastSlice];
        const double s1 = lastSlice + 1 < (int) slices.size() ? slices[(size_t) lastSlice + 1] : (double) sample->length();
        g.setColour (SamplerLookAndFeel::accent().withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (sampleToX (s0), 0.f, sampleToX (s1) - sampleToX (s0), (float) getHeight()));
    }
    g.drawImageAt (cache, 0, 0);

    g.setFont (juce::Font (11.f, juce::Font::bold));
    for (size_t i = 0; i < slices.size(); ++i)
    {
        const float x = sampleToX (slices[i]);
        if (x < -1.f || x > getWidth() + 1.f) continue;
        g.setColour (juce::Colour (0xffe2e2e2).withAlpha (0.85f));
        g.drawVerticalLine ((int) x, 0.f, (float) getHeight());
        g.setColour (juce::Colour (0xff0a0a0a));
        g.fillRect (juce::Rectangle<float> (x + 1.f, 1.f, 22.f, 14.f));
        g.setColour (SamplerLookAndFeel::accent());
        g.drawText (juce::String ((int) i + 1), (int) x + 3, 1, 20, 14, juce::Justification::centredLeft);
    }

    if (playPos >= 0)
    {
        g.setColour (juce::Colour (0xffff5a5a));
        g.drawVerticalLine ((int) sampleToX (playPos), 0.f, (float) getHeight());
    }
    if (scratchMode)
    {
        g.setColour (SamplerLookAndFeel::accent());
        g.setFont (juce::Font (12.f, juce::Font::bold));
        g.drawText ("SCRATCH: HOLD THE MOUSE AND MOVE LEFT / RIGHT", 0, getHeight() - 20, getWidth(), 18, juce::Justification::centred);
    }
}

void WaveView::mouseDown (const juce::MouseEvent& e)
{
    if (! sample) return;
    if (scratchMode) { proc.setScratch (true, juce::jlimit (0.0, (double) sample->length() - 2, xToSample ((float) e.x))); return; }
    if (e.mods.isPopupMenu())
    {
        const int i = nearestMarker ((float) e.x, 8.f);
        if (i > 0) { slices.erase (slices.begin() + i); proc.setSlices (slices); slices = proc.getSlices(); if (onSlicesEdited) onSlicesEdited(); repaint(); }
        return;
    }
    dragIdx = nearestMarker ((float) e.x, 6.f);
    if (dragIdx < 0)
    {
        const int pos = (int) juce::jlimit (1.0, (double) sample->length() - 3, xToSample ((float) e.x));
        slices.push_back (pos);
        std::sort (slices.begin(), slices.end());
        proc.setSlices (slices);
        slices = proc.getSlices();
        for (size_t i = 0; i < slices.size(); ++i) if (slices[i] == pos) dragIdx = (int) i;
        if (onSlicesEdited) onSlicesEdited();
        repaint();
    }
}

void WaveView::mouseDrag (const juce::MouseEvent& e)
{
    if (! sample) return;
    if (scratchMode) { proc.setScratch (true, juce::jlimit (0.0, (double) sample->length() - 2, xToSample ((float) e.x))); return; }
    if (dragIdx <= 0 || dragIdx >= (int) slices.size()) return;
    const int lo = slices[(size_t) dragIdx - 1] + 64;
    const int hi = dragIdx + 1 < (int) slices.size() ? slices[(size_t) dragIdx + 1] - 64 : sample->length() - 4;
    slices[(size_t) dragIdx] = juce::jlimit (lo, juce::jmax (lo, hi), (int) xToSample ((float) e.x));
    repaint();
}

void WaveView::mouseUp (const juce::MouseEvent& e)
{
    if (scratchMode) { proc.setScratch (false, 0.0); return; }
    if (dragIdx > 0)
    {
        proc.setSlices (slices);
        slices = proc.getSlices();
        if (onSlicesEdited) onSlicesEdited();
    }
    dragIdx = -1;
    juce::ignoreUnused (e);
    repaint();
}

void WaveView::mouseDoubleClick (const juce::MouseEvent&)
{
    if (! sample) return;
    viewStart = 0; viewLen = sample->length(); cacheDirty = true; repaint();
}

void WaveView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! sample) return;
    const double total = sample->length();
    if (e.mods.isShiftDown())
        viewStart += -w.deltaY * viewLen * 0.3;
    else
    {
        const double anchor = xToSample ((float) e.x);
        const double factor = std::exp (-w.deltaY * 0.6);
        const double nl = juce::jlimit (400.0, total, viewLen * factor);
        viewStart = anchor - (anchor - viewStart) * (nl / viewLen);
        viewLen = nl;
    }
    viewStart = juce::jlimit (0.0, juce::jmax (0.0, total - viewLen), viewStart);
    cacheDirty = true;
    repaint();
}

void WaveView::timerCallback()
{
    const double pp = proc.getPlayhead();
    const int ls = proc.getLastSlice();
    if (pp != playPos || ls != lastSlice) { playPos = pp; lastSlice = ls; repaint(); }
}

// ------------------------------------------------------------------ keyboard
void SliceKeyboard::drawWhiteNote (int n, juce::Graphics& g, juce::Rectangle<float> a, bool down, bool over, juce::Colour line, juce::Colour)
{
    const bool on = n >= first && n < first + count;
    juce::Colour c = on ? juce::Colour (0xffcfe9d4) : juce::Colour (0xff3c3c3c);
    if (down) c = SamplerLookAndFeel::accent();
    else if (over) c = c.brighter (0.15f);
    g.setColour (c);
    g.fillRect (a);
    g.setColour (line);
    g.fillRect (a.getRight() - 1.f, a.getY(), 1.f, a.getHeight());
}

void SliceKeyboard::drawBlackNote (int n, juce::Graphics& g, juce::Rectangle<float> a, bool down, bool over, juce::Colour)
{
    const bool on = n >= first && n < first + count;
    juce::Colour c = on ? juce::Colour (0xff1d3a25) : juce::Colour (0xff0a0a0a);
    if (down) c = SamplerLookAndFeel::accent().darker (0.2f);
    else if (over) c = c.brighter (0.2f);
    g.setColour (c);
    g.fillRect (a);
}

// ------------------------------------------------------------------ editor
SamplerEditor::SamplerEditor (SamplerProcessor& p)
    : AudioProcessorEditor (&p), proc (p), wave (p), keyboard (p.keyboardState)
{
    setLookAndFeel (&laf);
    setSize (760, 664);

    addAndMakeVisible (wave);
    wave.onSlicesEdited = [this] { refreshInfo(); };
    wave.sampleChanged();

    for (auto* b : { &loadBtn, &gridBtn, &autoBtn, &clearBtn, &scratchBtn }) addAndMakeVisible (b);
    for (auto* b : { &reverseBtn, &loopBtn, &monoBtn, &keyBtn, &syncBtn }) { addAndMakeVisible (b); b->setClickingTogglesState (true); }
    scratchBtn.setClickingTogglesState (true);

    fileLabel.setFont (juce::Font (14.f, juce::Font::bold));
    fileLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe2e2e2));
    infoLabel.setFont (juce::Font (12.f));
    infoLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (fileLabel);
    addAndMakeVisible (infoLabel);

    gridBox.addItemList ({ "4", "8", "16", "32", "64" }, 1);
    modeBox.addItemList ({ "ONE-SHOT", "GATE" }, 1);
    retrigBox.addItemList ({ "RETRIG OFF", "1/4", "1/8", "1/16", "1/32", "1/64" }, 1);
    for (auto* c : { &gridBox, &modeBox, &retrigBox }) addAndMakeVisible (c);
    comboAtt.push_back (std::make_unique<CA> (proc.apvts, "grid", gridBox));
    comboAtt.push_back (std::make_unique<CA> (proc.apvts, "mode", modeBox));
    comboAtt.push_back (std::make_unique<CA> (proc.apvts, "retrig", retrigBox));

    struct K { juce::Slider* s; juce::Label* l; const char* name; const char* id; };
    for (auto& k : { K { &gain, &gainL, "GAIN", "gain" }, K { &pitch, &pitchL, "PITCH", "pitch" }, K { &attack, &attackL, "ATTACK", "attack" },
                     K { &release, &releaseL, "RELEASE", "release" }, K { &srcbpm, &srcbpmL, "SRC BPM", "srcbpm" }, K { &chaos, &chaosL, "CHAOS", "chaos" },
                     K { &bits, &bitsL, "BITS", "bits" }, K { &rate, &rateL, "RATE", "rate" }, K { &drive, &driveL, "DRIVE", "drive" },
                     K { &revchance, &revchanceL, "REV %", "revchance" }, K { &repstep, &repstepL, "REPEAT ST", "repstep" },
                     K { &startnote, &startnoteL, "START KEY", "startnote" } })
    {
        setupKnob (*k.s, *k.l, k.name);
        sliderAtt.push_back (std::make_unique<SA> (proc.apvts, k.id, *k.s));
    }
    sens.setSliderStyle (juce::Slider::LinearHorizontal);
    sens.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
    addAndMakeVisible (sens);
    sliderAtt.push_back (std::make_unique<SA> (proc.apvts, "sens", sens));

    buttonAtt.push_back (std::make_unique<BA> (proc.apvts, "reverse", reverseBtn));
    buttonAtt.push_back (std::make_unique<BA> (proc.apvts, "loop", loopBtn));
    buttonAtt.push_back (std::make_unique<BA> (proc.apvts, "mono", monoBtn));
    buttonAtt.push_back (std::make_unique<BA> (proc.apvts, "keypitch", keyBtn));
    buttonAtt.push_back (std::make_unique<BA> (proc.apvts, "sync", syncBtn));

    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load audio", juce::File(), "*.mp3;*.wav;*.flac;*.ogg;*.aif;*.aiff");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { const auto f = fc.getResult(); if (f.existsAsFile()) loadFile (f); });
    };
    gridBtn.onClick = [this] { proc.sliceGrid (gridBox.getText().getIntValue()); wave.slicesChanged(); refreshInfo(); };
    autoBtn.onClick = [this] { proc.sliceTransient ((float) sens.getValue()); wave.slicesChanged(); refreshInfo(); };
    clearBtn.onClick = [this] { proc.setSlices ({ 0 }); wave.slicesChanged(); refreshInfo(); };
    scratchBtn.onClick = [this] { wave.scratchMode = scratchBtn.getToggleState(); if (! wave.scratchMode) proc.setScratch (false, 0.0); wave.repaint(); };

    addAndMakeVisible (keyboard);
    keyboard.setAvailableRange (36, 96);
    keyboard.setKeyWidth (20.f);
    keyboard.setLowestVisibleKey (36);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setMidiChannel (1);
    keyboard.setWantsKeyboardFocus (false);
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff3a3a3a));

    refreshInfo();
    startTimerHz (6);
}

SamplerEditor::~SamplerEditor()
{
    proc.setScratch (false, 0.0);
    setLookAndFeel (nullptr);
}

void SamplerEditor::setupKnob (juce::Slider& s, juce::Label& l, const juce::String& name)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 16);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    addAndMakeVisible (s);
    l.setText (name, juce::dontSendNotification);
    l.setJustificationType (juce::Justification::centred);
    l.setFont (juce::Font (11.f, juce::Font::bold));
    l.attachToComponent (&s, false);
    addAndMakeVisible (l);
}

void SamplerEditor::loadFile (const juce::File& f)
{
    if (proc.loadFile (f))
    {
        wave.sampleChanged();
        refreshInfo();
    }
    else
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Cannot load file",
                                                "This file could not be read (or it is longer than 12 minutes).");
}

bool SamplerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const auto e = juce::File (f).getFileExtension().toLowerCase();
        if (e == ".mp3" || e == ".wav" || e == ".flac" || e == ".ogg" || e == ".aif" || e == ".aiff") return true;
    }
    return false;
}

void SamplerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (isInterestedInFileDrag (juce::StringArray (f))) { loadFile (juce::File (f)); break; }
}

void SamplerEditor::timerCallback()
{
    // keep the keyboard highlight in sync with the start key / slice count
    const int cnt = (int) proc.getSlices().size(), st = proc.getStartNote();
    if (cnt != keyboard.count || st != keyboard.first) { keyboard.count = cnt; keyboard.first = st; keyboard.repaint(); refreshInfo(); }
}

static juce::String flName (int n)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[n % 12]) + juce::String (n / 12);
}

void SamplerEditor::refreshInfo()
{
    const auto f = proc.getLoadedFile();
    fileLabel.setText (f.existsAsFile() ? f.getFileName() : "NO FILE LOADED", juce::dontSendNotification);
    const int cnt = (int) proc.getSlices().size(), st = proc.getStartNote();
    infoLabel.setText (proc.getSample() ? juce::String (cnt) + " slices  /  keys " + flName (st) + " - " + flName (st + cnt - 1)
                                        : juce::String(), juce::dontSendNotification);
    keyboard.first = st; keyboard.count = proc.getSample() ? cnt : 0;
    keyboard.repaint();
}

void SamplerEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0a0a0a));
    g.setColour (juce::Colour (0xffe2e2e2));
    g.setFont (juce::Font (15.f, juce::Font::bold));
    g.drawText ("INFINITI HYPE SAMPLER", 20, 12, 300, 22, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff6a6a6a));
    g.setFont (juce::Font (12.f));
    g.drawText ("SLICE  /  CHOP  /  SCRATCH", 340, 12, 400, 22, juce::Justification::centredRight);
    g.setColour (juce::Colour (0xff2a2a2a));
    g.drawHorizontalLine (38, 20.f, 740.f);
    g.setColour (juce::Colour (0xff6a6a6a));
    g.setFont (juce::Font (11.f));
    g.drawText ("click: add slice   drag: move   right-click: delete   wheel: zoom   shift+wheel: pan   double-click: reset zoom",
                20, 322, 720, 14, juce::Justification::centredLeft);
}

void SamplerEditor::resized()
{
    const int mx = 20, w = getWidth() - 2 * mx;
    loadBtn.setBounds (mx, 46, 70, 28);
    fileLabel.setBounds (mx + 78, 46, 360, 28);
    infoLabel.setBounds (mx + 440, 46, w - 440, 28);

    int x = mx; const int y = 80, h = 28;
    gridBox.setBounds (x, y, 56, h); x += 60;
    gridBtn.setBounds (x, y, 100, h); x += 108;
    sens.setBounds (x, y, 150, h); x += 156;
    autoBtn.setBounds (x, y, 100, h); x += 108;
    clearBtn.setBounds (x, y, 70, h); x += 78;
    scratchBtn.setBounds (x, y, mx + w - x, h);

    wave.setBounds (mx, 116, w, 200);

    const int kn = 84, side = mx + 6 * (kn + 6) + 20, sideW = mx + w - side;
    int k = 0;
    for (auto* s : { &gain, &pitch, &attack, &release, &srcbpm, &chaos }) s->setBounds (mx + k++ * (kn + 6), 356, kn, 84);
    k = 0;
    for (auto* s : { &bits, &rate, &drive, &revchance, &repstep, &startnote }) s->setBounds (mx + k++ * (kn + 6), 466, kn, 84);

    modeBox.setBounds (side, 358, sideW, 26);
    retrigBox.setBounds (side, 390, sideW, 26);
    const int tw = (sideW - 6) / 2;
    reverseBtn.setBounds (side, 424, tw, 28);       loopBtn.setBounds (side + tw + 6, 424, tw, 28);
    monoBtn.setBounds (side, 458, tw, 28);          keyBtn.setBounds (side + tw + 6, 458, tw, 28);
    syncBtn.setBounds (side, 492, sideW, 28);

    keyboard.setBounds (mx, 566, w, 80);
}
