#include "SamplerProcessor.h"
#include "SamplerEditor.h"

static juce::String noteNameFL (int n)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12);   // FL Studio naming: MIDI 60 = C5
}

juce::AudioProcessorValueTreeState::ParameterLayout SamplerProcessor::createLayout()
{
    using juce::NormalisableRange;
    using juce::ParameterID;
    using Attr = juce::AudioParameterFloatAttributes;

    auto fmt = [] (int decimals, float mult, const char* suffix)
    {
        return Attr().withStringFromValueFunction ([=] (float v, int)
        {
            return (decimals == 0 ? juce::String (juce::roundToInt (v * mult)) : juce::String (v * mult, decimals)) + suffix;
        });
    };
    auto pct = [] {
        return Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.f)) + "%"; })
                     .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue() / 100.f; });
    };
    auto flt = [] (const char* id, const char* name, float lo, float hi, float step, float def, Attr a, float skew = 1.f)
    {
        return std::make_unique<juce::AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, step, skew), def, a);
    };

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (flt ("gain", "Gain", 0.f, 1.5f, 0.01f, 0.8f, fmt (0, 100.f, "%")));
    p.push_back (flt ("pitch", "Pitch", -24.f, 24.f, 0.01f, 0.f, fmt (1, 1.f, " st")));
    p.push_back (flt ("attack", "Attack", 0.f, 200.f, 0.1f, 0.f, fmt (0, 1.f, " ms"), 0.5f));
    p.push_back (flt ("release", "Release", 1.f, 1000.f, 1.f, 30.f, fmt (0, 1.f, " ms"), 0.4f));
    p.push_back (flt ("srcbpm", "Source BPM", 60.f, 300.f, 0.1f, 170.f, fmt (1, 1.f, "")));
    p.push_back (flt ("chaos", "Chaos", 0.f, 1.f, 0.01f, 0.f, pct()));
    p.push_back (flt ("bits", "Bits", 3.f, 16.f, 1.f, 16.f, fmt (0, 1.f, "")));
    p.push_back (flt ("rate", "Rate", 2000.f, 44100.f, 1.f, 44100.f, fmt (0, 1.f, " Hz"), 0.4f));
    p.push_back (flt ("drive", "Drive", 0.f, 1.f, 0.01f, 0.f, pct()));
    p.push_back (flt ("revchance", "Rev Chance", 0.f, 1.f, 0.01f, 0.f, pct()));
    p.push_back (flt ("repstep", "Repeat Step", -12.f, 12.f, 0.01f, 0.f, fmt (1, 1.f, " st")));
    p.push_back (flt ("sens", "Slice Sens", 0.f, 1.f, 0.01f, 0.5f, pct()));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (ParameterID { "startnote", 1 }, "Start Key",
                 NormalisableRange<float> (12.f, 72.f, 1.f), 36.f,
                 Attr().withStringFromValueFunction ([] (float v, int) { return noteNameFL ((int) v); })));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", juce::StringArray { "ONE-SHOT", "GATE" }, 0));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (ParameterID { "retrig", 1 }, "Retrigger",
                 juce::StringArray { "RETRIG OFF", "1/4", "1/8", "1/16", "1/32", "1/64" }, 0));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (ParameterID { "grid", 1 }, "Grid", juce::StringArray { "4", "8", "16", "32", "64" }, 2));
    p.push_back (std::make_unique<juce::AudioParameterBool> (ParameterID { "reverse", 1 }, "Reverse", false));
    p.push_back (std::make_unique<juce::AudioParameterBool> (ParameterID { "loop", 1 }, "Loop", false));
    p.push_back (std::make_unique<juce::AudioParameterBool> (ParameterID { "mono", 1 }, "Mono", false));
    p.push_back (std::make_unique<juce::AudioParameterBool> (ParameterID { "keypitch", 1 }, "Key Pitch", false));
    p.push_back (std::make_unique<juce::AudioParameterBool> (ParameterID { "sync", 1 }, "Sync", false));
    return { p.begin(), p.end() };
}

SamplerProcessor::SamplerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    formats.registerBasicFormats();
}

void SamplerProcessor::pushToEngine()
{
    {
        const juce::SpinLock::ScopedLockType l (lock);
        pendingSample = sampleMsg;
        pendingSlices = slicesMsg;
    }
    dirty = true;
}

bool SamplerProcessor::loadFile (const juce::File& f, bool resetSlices)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0) return false;
    if ((double) reader->lengthInSamples > reader->sampleRate * 60.0 * 12.0) return false;   // 12 min limit
    const int len = (int) reader->lengthInSamples;

    juce::AudioBuffer<float> buf (2, len);
    buf.clear();
    if (! reader->read (&buf, 0, len, 0, true, true)) return false;
    if (reader->numChannels < 2) buf.copyFrom (1, 0, buf, 0, 0, len);

    auto d = std::make_shared<ihs::SampleData>();
    d->sampleRate = reader->sampleRate;
    d->l.assign (buf.getReadPointer (0), buf.getReadPointer (0) + len);
    d->r.assign (buf.getReadPointer (1), buf.getReadPointer (1) + len);

    sampleMsg = d;
    loadedFile = f;
    apvts.state.setProperty ("file", f.getFullPathName(), nullptr);
    if (resetSlices)
    {
        sliceGrid (16);
    }
    else
        pushToEngine();
    return true;
}

void SamplerProcessor::setSlices (std::vector<int> pts)
{
    const int len = sampleMsg ? sampleMsg->length() : 0;
    pts.push_back (0);
    std::sort (pts.begin(), pts.end());
    pts.erase (std::unique (pts.begin(), pts.end()), pts.end());
    pts.erase (std::remove_if (pts.begin(), pts.end(), [len] (int p) { return p < 0 || (len > 0 && p >= len - 2); }), pts.end());
    if (pts.size() > 64) pts.resize (64);
    if (pts.empty()) pts.push_back (0);

    juce::StringArray sa;
    for (int p : pts) sa.add (juce::String (p));
    apvts.state.setProperty ("slices", sa.joinIntoString (","), nullptr);
    slicesMsg = std::make_shared<ihs::SlicePoints> (std::move (pts));
    pushToEngine();
}

void SamplerProcessor::sliceGrid (int count)
{
    if (! sampleMsg) return;
    setSlices (ihs::gridSlices (sampleMsg->length(), count));
}

void SamplerProcessor::sliceTransient (float sens)
{
    if (! sampleMsg) return;
    setSlices (ihs::transientSlices (*sampleMsg, sens));
}

void SamplerProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    keyboardState.reset();
}

bool SamplerProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo()) && l.getMainInputChannelSet().isDisabled();
}

void SamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    if (dirty.load())
    {
        const juce::SpinLock::ScopedTryLockType l (lock);
        if (l.isLocked())
        {
            engine.setSample (pendingSample);
            engine.setSlices (pendingSlices);
            dirty = false;
        }
    }

    auto val = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    ihs::EngineParams p;
    p.gain = val ("gain"); p.semis = val ("pitch");
    p.attackMs = val ("attack"); p.releaseMs = val ("release"); p.srcBpm = val ("srcbpm");
    p.chaos = val ("chaos"); p.bits = val ("bits"); p.rate = val ("rate"); p.drive = val ("drive");
    p.revChance = val ("revchance"); p.retrigStep = val ("repstep"); p.startNote = (int) val ("startnote");
    p.gateMode = (int) val ("mode") == 1;
    static const int divs[] = { 0, 4, 8, 16, 32, 64 };
    p.retrigDiv = divs[juce::jlimit (0, 5, (int) val ("retrig"))];
    p.reverse = val ("reverse") > 0.5f; p.loop = val ("loop") > 0.5f; p.mono = val ("mono") > 0.5f;
    p.keyPitch = val ("keypitch") > 0.5f; p.sync = val ("sync") > 0.5f;
    engine.setParams (p);

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = *b;

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    std::vector<float> dummy;
    if (R == nullptr) { dummy.assign ((size_t) n, 0.f); R = dummy.data(); }

    int done = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > done) { engine.process (L + done, R + done, at - done, bpm); done = at; }
        const auto m = meta.getMessage();
        if (m.isNoteOn()) engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allOff();
    }
    if (done < n) engine.process (L + done, R + done, n - done, bpm);
}

juce::AudioProcessorEditor* SamplerProcessor::createEditor() { return new SamplerEditor (*this); }

void SamplerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void SamplerProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            const juce::File f (apvts.state.getProperty ("file", "").toString());
            if (f.existsAsFile() && loadFile (f, false))
            {
                std::vector<int> pts;
                for (auto& s : juce::StringArray::fromTokens (apvts.state.getProperty ("slices", "").toString(), ",", ""))
                    if (s.isNotEmpty()) pts.push_back (s.getIntValue());
                if (pts.empty()) sliceGrid (16); else setSlices (pts);
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SamplerProcessor(); }
