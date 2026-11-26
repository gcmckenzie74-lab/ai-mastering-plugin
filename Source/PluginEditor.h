#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class AIMasterEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit AIMasterEditor (AIMasterProcessor&);
    ~AIMasterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void analyseClicked();
    void drawLoudnessBar (juce::Graphics&, juce::Rectangle<int> area);

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name);

    AIMasterProcessor& processor;

    Knob target, ceiling, release, trim;
    juce::TextButton analyseButton { "Analyse" };
    juce::TextButton resetButton { "Reset" };
    juce::Label statusLabel;

    // cached read-outs
    float inLufs = -100.0f, outMomentary = -100.0f, outShortTerm = -100.0f;
    float grDisplay = 0.0f, analysisLufs = -100.0f, gainDb = 0.0f;

    juce::Rectangle<int> meterArea, readoutArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIMasterEditor)
};
