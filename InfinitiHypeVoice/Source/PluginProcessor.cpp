#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout InfinitiProcessor::createLayout()
{
    using juce::NormalisableRange;
    using juce::ParameterID;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<juce::AudioParameterFloat> (ParameterID { "speed", 1 }, "Speed", NormalisableRange<float> (0.5f, 2.0f, 0.01f), 1.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (ParameterID { "pitch", 1 }, "Pitch", NormalisableRange<float> (-12.f, 12.f, 0.01f), 0.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (ParameterID { "formant", 1 }, "Formant", NormalisableRange<float> (0.7f, 1.4f, 0.01f), 1.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (ParameterID { "bits", 1 }, "Bits", NormalisableRange<float> (3.f, 16.f, 1.f), 8.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (ParameterID { "rate", 1 }, "Rate", NormalisableRange<float> (2000.f, 44100.f, 1.f, 0.4f), 11025.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (ParameterID { "volume", 1 }, "Volume", NormalisableRange<float> (0.f, 1.5f, 0.01f), 0.8f));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", juce::StringArray { "PHRASE", "WORD STEP" }, 0));
    p.push_back (std::make_unique<juce::AudioParameterBool> (ParameterID { "gate", 1 }, "Gate", false));
    return { p.begin(), p.end() };
}

InfinitiProcessor::InfinitiProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    setText ("INFINITI HYPE MACHINE");
}

void InfinitiProcessor::compile()
{
    auto u = ihm::buildUtterance (text.toStdString());
    {
        const juce::SpinLock::ScopedLockType l (uttLock);
        utt = std::move (u);
    }
    wordCounter = 0;
}

void InfinitiProcessor::setText (const juce::String& t)
{
    text = t;
    apvts.state.setProperty ("text", t, nullptr);
    compile();
}

void InfinitiProcessor::prepareToPlay (double sampleRate, int)
{
    voice.prepare (sampleRate);
    keyboardState.reset();
}

bool InfinitiProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return (o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo()) && l.getMainInputChannelSet().isDisabled();
}

void InfinitiProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    auto val = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    ihm::Params p;
    p.speed = val ("speed"); p.semis = val ("pitch"); p.formant = val ("formant");
    p.bits = val ("bits"); p.rate = val ("rate"); p.volume = val ("volume");
    p.gate = val ("gate") > 0.5f;
    voice.setParams (p);
    const bool wordMode = (int) val ("mode") == 1;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            const bool playing = pos->getIsPlaying();
            if (playing && ! wasPlaying) wordCounter = 0;
            wasPlaying = playing;
        }

    float* out = buffer.getWritePointer (0);
    int done = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > done) { voice.process (out + done, at - done); done = at; }
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            std::shared_ptr<const ihm::Utterance> u;
            { const juce::SpinLock::ScopedLockType l (uttLock); u = utt; }
            voice.setUtterance (u);
            int w = -1;
            if (wordMode && u && u->numWords() > 0) w = (wordCounter++) % u->numWords();
            voice.noteOn (m.getNoteNumber(), m.getFloatVelocity(), w);
        }
        else if (m.isNoteOff()) voice.noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) voice.allOff();
    }
    if (done < n) voice.process (out + done, n - done);

    for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, n);
}

juce::AudioProcessorEditor* InfinitiProcessor::createEditor() { return new InfinitiEditor (*this); }

void InfinitiProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void InfinitiProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            text = apvts.state.getProperty ("text", "INFINITI HYPE MACHINE").toString();
            compile();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new InfinitiProcessor(); }
