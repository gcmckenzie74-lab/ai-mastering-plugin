#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "LoudnessMeter.h"
#include "LookaheadLimiter.h"

/**
    AI Master - stage 1: loudness analysis + auto-gain + lookahead limiter.

    Workflow
      1. Press "Analyse" and play the loudest / most representative part of the song.
      2. Press "Stop". The plugin compares the measured output loudness with the
         target LUFS and sets the auto gain so the master hits the target.
      3. Running "Analyse" a second time refines the result (it then accounts
         for the loudness the limiter takes away).

    Changing the target afterwards shifts the auto gain by the same amount, so a
    new analysis pass is only needed if the mix changes.
*/
class AIMasterProcessor : public juce::AudioProcessor
{
public:
    AIMasterProcessor();
    ~AIMasterProcessor() override = default;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Analysis control (call from the UI thread)
    void startAnalysis();
    /** Stops the analysis pass and applies the result. Returns false if there was too little audio. */
    bool stopAnalysis();
    bool isAnalysing() const noexcept { return analysing.load(); }
    void clearAnalysis();

    // Read-outs for the UI
    bool hasAnalysis() const noexcept         { return learned.load(); }
    float getEffectiveGainDb() const;
    float getInputShortTermLufs() const       { return inputMeter.getShortTermLufs(); }
    float getOutputMomentaryLufs() const      { return outputMeter.getMomentaryLufs(); }
    float getOutputShortTermLufs() const      { return outputMeter.getShortTermLufs(); }
    float getAnalysisIntegratedLufs() const   { return analysisMeter.getIntegratedLufs(); }
    float getGainReductionDb() const          { return gainReductionDb.load(); }

    juce::AudioProcessorValueTreeState parameters;

    static constexpr const char* paramTarget  = "target";
    static constexpr const char* paramCeiling = "ceiling";
    static constexpr const char* paramRelease = "release";
    static constexpr const char* paramTrim    = "trim";

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::atomic<float>* targetParam  = nullptr;
    std::atomic<float>* ceilingParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* trimParam    = nullptr;

    LoudnessMeter inputMeter, outputMeter, analysisMeter;
    LookaheadLimiter limiter;
    juce::SmoothedValue<float> gainSmoother;

    // Result of the last analysis
    std::atomic<bool>  learned { false };
    std::atomic<float> autoGainDb { 0.0f };       // gain chosen by the analysis
    std::atomic<float> learnedForTarget { -14.0f }; // target LUFS at the time of analysis

    std::atomic<bool> analysing { false };
    std::atomic<bool> resetAnalysisRequested { false };
    std::atomic<float> gainReductionDb { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIMasterProcessor)
};
