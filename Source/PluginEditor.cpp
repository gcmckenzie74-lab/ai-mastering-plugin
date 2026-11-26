#include "PluginEditor.h"

namespace Colours
{
    const juce::Colour background { 0xff15171c };
    const juce::Colour panel      { 0xff1f232b };
    const juce::Colour accent     { 0xff4fc3f7 };
    const juce::Colour warn       { 0xffffb74d };
    const juce::Colour text       { 0xffe6e9ef };
    const juce::Colour dim        { 0xff8b93a1 };
}

static juce::String formatLufs (float v)
{
    return v <= -70.0f ? juce::String ("--") : juce::String (v, 1);
}

AIMasterEditor::AIMasterEditor (AIMasterProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setupKnob (target,  AIMasterProcessor::paramTarget,  "Target LUFS");
    setupKnob (ceiling, AIMasterProcessor::paramCeiling, "Ceiling");
    setupKnob (release, AIMasterProcessor::paramRelease, "Release");
    setupKnob (trim,    AIMasterProcessor::paramTrim,    "Trim");

    analyseButton.setColour (juce::TextButton::buttonColourId, Colours::panel);
    analyseButton.setColour (juce::TextButton::buttonOnColourId, Colours::accent.darker (0.3f));
    analyseButton.onClick = [this] { analyseClicked(); };
    addAndMakeVisible (analyseButton);

    resetButton.setColour (juce::TextButton::buttonColourId, Colours::panel);
    resetButton.onClick = [this]
    {
        processor.clearAnalysis();
        statusLabel.setText ("Analysis cleared. Press Analyse and play the loudest section.",
                             juce::dontSendNotification);
    };
    addAndMakeVisible (resetButton);

    statusLabel.setColour (juce::Label::textColourId, Colours::dim);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setText (processor.hasAnalysis()
                             ? "Analysis loaded. Adjust Target to change loudness."
                             : "Press Analyse and play the loudest 20-30 seconds of the song.",
                         juce::dontSendNotification);
    addAndMakeVisible (statusLabel);

    setSize (640, 380);
    startTimerHz (20);
}

AIMasterEditor::~AIMasterEditor()
{
    stopTimer();
}

void AIMasterEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, Colours::panel.brighter (0.2f));
    k.slider.setColour (juce::Slider::thumbColourId, Colours::text);
    k.slider.setColour (juce::Slider::textBoxTextColourId, Colours::text);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, Colours::dim);
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.parameters, paramId, k.slider);
}

void AIMasterEditor::analyseClicked()
{
    if (! processor.isAnalysing())
    {
        processor.startAnalysis();
        analyseButton.setButtonText ("Stop && Apply");
        analyseButton.setToggleState (true, juce::dontSendNotification);
        statusLabel.setText ("Listening... play the loudest section, then press Stop & Apply.",
                             juce::dontSendNotification);
    }
    else
    {
        const bool ok = processor.stopAnalysis();
        analyseButton.setButtonText ("Analyse");
        analyseButton.setToggleState (false, juce::dontSendNotification);
        statusLabel.setText (ok ? "Applied. Run Analyse again to refine (accounts for limiting)."
                                : "No audio detected - press Analyse and press play in Cubase.",
                             juce::dontSendNotification);
    }
}

void AIMasterEditor::timerCallback()
{
    inLufs       = processor.getInputShortTermLufs();
    outMomentary = processor.getOutputMomentaryLufs();
    outShortTerm = processor.getOutputShortTermLufs();
    analysisLufs = processor.getAnalysisIntegratedLufs();
    gainDb       = processor.getEffectiveGainDb();

    // peak-hold with decay for the gain reduction read-out
    const float gr = processor.getGainReductionDb();
    grDisplay = gr > grDisplay ? gr : grDisplay * 0.9f;

    repaint (meterArea);
    repaint (readoutArea);
}

void AIMasterEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    // title
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("AI MASTER", 20, 12, 200, 30, juce::Justification::centredLeft);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("loudness analysis  >  auto gain  >  lookahead limiter", 180, 12, 400, 30,
                juce::Justification::centredLeft);

    // read-outs
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (readoutArea.toFloat(), 6.0f);

    auto r = readoutArea.reduced (12, 8);
    const int rowH = r.getHeight() / 6;

    auto row = [&] (const juce::String& name, const juce::String& value, juce::Colour c)
    {
        auto line = r.removeFromTop (rowH);
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (name, line, juce::Justification::centredLeft);
        g.setColour (c);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (value, line, juce::Justification::centredRight);
    };

    row ("Input (short-term)",   formatLufs (inLufs) + " LUFS", Colours::text);
    row ("Output (momentary)",   formatLufs (outMomentary) + " LUFS", Colours::text);
    row ("Output (short-term)",  formatLufs (outShortTerm) + " LUFS", Colours::accent);
    row ("Auto gain",            (gainDb >= 0 ? "+" : "") + juce::String (gainDb, 1) + " dB",
                                 processor.hasAnalysis() ? Colours::accent : Colours::dim);
    row ("Limiter reduction",    "-" + juce::String (grDisplay, 1) + " dB",
                                 grDisplay > 6.0f ? Colours::warn : Colours::text);
    row ("Analysis (integrated)", processor.isAnalysing() ? formatLufs (analysisLufs) + " LUFS"
                                                          : juce::String ("idle"),
                                  processor.isAnalysing() ? Colours::warn : Colours::dim);

    drawLoudnessBar (g, meterArea);
}

void AIMasterEditor::drawLoudnessBar (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (area.toFloat(), 4.0f);

    const float minL = -36.0f, maxL = 0.0f;
    auto toX = [&] (float lufs)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (lufs - minL) / (maxL - minL));
        return (float) area.getX() + t * (float) area.getWidth();
    };

    auto bar = area.toFloat().reduced (2.0f);
    const float x = toX (outShortTerm);
    g.setColour (Colours::accent.withAlpha (0.8f));
    g.fillRoundedRectangle (bar.withRight (juce::jmax (bar.getX(), x)), 3.0f);

    // target marker
    const float tx = toX (processor.parameters.getRawParameterValue (AIMasterProcessor::paramTarget)->load());
    g.setColour (Colours::warn);
    g.fillRect (juce::Rectangle<float> (tx - 1.0f, (float) area.getY() - 4.0f, 2.0f, (float) area.getHeight() + 8.0f));
}

void AIMasterEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (36); // title

    auto knobs = area.removeFromTop (150);
    readoutArea = knobs.removeFromRight (260);
    knobs.removeFromRight (12);
    const int w = knobs.getWidth() / 4;

    for (auto* k : { &target, &ceiling, &release, &trim })
    {
        auto cell = knobs.removeFromLeft (w);
        k->label.setBounds (cell.removeFromTop (20));
        k->slider.setBounds (cell.reduced (4, 0));
    }

    area.removeFromTop (20);
    meterArea = area.removeFromTop (18);

    area.removeFromTop (24);
    auto buttons = area.removeFromTop (36);
    analyseButton.setBounds (buttons.removeFromLeft (160));
    buttons.removeFromLeft (10);
    resetButton.setBounds (buttons.removeFromLeft (90));

    area.removeFromTop (10);
    statusLabel.setBounds (area.removeFromTop (24));
}
