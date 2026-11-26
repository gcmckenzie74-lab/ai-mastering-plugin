#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float minGainDb = -24.0f;
    constexpr float maxGainDb = 24.0f;
}

AIMasterProcessor::AIMasterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "AIMaster", createLayout())
{
    targetParam  = parameters.getRawParameterValue (paramTarget);
    ceilingParam = parameters.getRawParameterValue (paramCeiling);
    releaseParam = parameters.getRawParameterValue (paramRelease);
    trimParam    = parameters.getRawParameterValue (paramTrim);
}

juce::AudioProcessorValueTreeState::ParameterLayout AIMasterProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using Attr = juce::AudioParameterFloatAttributes;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<P> (juce::ParameterID { paramTarget, 1 }, "Target Loudness",
                                           juce::NormalisableRange<float> (-20.0f, -6.0f, 0.1f), -14.0f,
                                           Attr().withLabel ("LUFS")));

    params.push_back (std::make_unique<P> (juce::ParameterID { paramCeiling, 1 }, "Ceiling",
                                           juce::NormalisableRange<float> (-3.0f, 0.0f, 0.1f), -1.0f,
                                           Attr().withLabel ("dBFS")));

    params.push_back (std::make_unique<P> (juce::ParameterID { paramRelease, 1 }, "Release",
                                           juce::NormalisableRange<float> (10.0f, 1000.0f, 1.0f, 0.4f), 120.0f,
                                           Attr().withLabel ("ms")));

    params.push_back (std::make_unique<P> (juce::ParameterID { paramTrim, 1 }, "Trim",
                                           juce::NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 0.0f,
                                           Attr().withLabel ("dB")));

    return { params.begin(), params.end() };
}

bool AIMasterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void AIMasterProcessor::prepareToPlay (double sampleRate, int)
{
    const int numCh = getTotalNumOutputChannels();

    inputMeter.prepare (sampleRate, numCh);
    outputMeter.prepare (sampleRate, numCh);
    analysisMeter.prepare (sampleRate, numCh);
    limiter.prepare (sampleRate, numCh, 5.0);

    setLatencySamples (limiter.getLatencySamples());

    gainSmoother.reset (sampleRate, 0.05);
    gainSmoother.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (getEffectiveGainDb()));
}

float AIMasterProcessor::getEffectiveGainDb() const
{
    float gain = trimParam->load();

    if (learned.load())
        gain += autoGainDb.load() + (targetParam->load() - learnedForTarget.load());

    return juce::jlimit (minGainDb, maxGainDb, gain);
}

void AIMasterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    inputMeter.process (buffer);

    // gain
    gainSmoother.setTargetValue (juce::Decibels::decibelsToGain (getEffectiveGainDb()));
    if (gainSmoother.isSmoothing())
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float g = gainSmoother.getNextValue();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, i, buffer.getSample (ch, i) * g);
        }
    }
    else
    {
        buffer.applyGain (gainSmoother.getTargetValue());
    }

    // limiter
    limiter.process (buffer, ceilingParam->load(), releaseParam->load());
    gainReductionDb.store (limiter.takeMaxGainReductionDb());

    // metering
    outputMeter.process (buffer);

    if (resetAnalysisRequested.exchange (false))
        analysisMeter.reset();

    if (analysing.load())
        analysisMeter.process (buffer);
}

//==============================================================================
void AIMasterProcessor::startAnalysis()
{
    resetAnalysisRequested.store (true);
    analysing.store (true);
}

bool AIMasterProcessor::stopAnalysis()
{
    analysing.store (false);

    const float measured = analysisMeter.getIntegratedLufs();
    if (measured <= -70.0f)
        return false; // nothing (or only silence) was played

    // The output was measured with the current gain applied, so correct that gain
    // by the difference between target and measured loudness.
    const float target = targetParam->load();
    const float currentGain = getEffectiveGainDb();
    const float newGain = juce::jlimit (minGainDb, maxGainDb, currentGain + (target - measured));

    autoGainDb.store (newGain - trimParam->load());
    learnedForTarget.store (target);
    learned.store (true);
    return true;
}

void AIMasterProcessor::clearAnalysis()
{
    analysing.store (false);
    learned.store (false);
    autoGainDb.store (0.0f);
}

//==============================================================================
void AIMasterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("learned", learned.load(), nullptr);
    state.setProperty ("autoGainDb", autoGainDb.load(), nullptr);
    state.setProperty ("learnedForTarget", learnedForTarget.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AIMasterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.hasType (parameters.state.getType()))
            return;

        learned.store ((bool) state.getProperty ("learned", false));
        autoGainDb.store ((float) state.getProperty ("autoGainDb", 0.0f));
        learnedForTarget.store ((float) state.getProperty ("learnedForTarget", -14.0f));
        parameters.replaceState (state);
    }
}

juce::AudioProcessorEditor* AIMasterProcessor::createEditor()
{
    return new AIMasterEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AIMasterProcessor();
}
