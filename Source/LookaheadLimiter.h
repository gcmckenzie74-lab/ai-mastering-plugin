#pragma once

#include <cmath>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

/**
    Transparent lookahead brickwall limiter.

    Gain computer:
      1. required gain per sample  g = min(1, ceiling / |peak across channels|)
      2. sliding minimum over the lookahead window (monotonic deque, O(1))
      3. release smoothing (only ever moves *up* slowly, never above the held minimum)
      4. moving average over the lookahead window -> smooth attack with no overshoot

    The audio is delayed by (lookahead - 1) samples, which is reported to the
    host as latency. Because every value in the averaging window already
    contains the required gain of the sample being output, the result never
    exceeds the ceiling (a final clamp catches floating-point rounding).
*/
class LookaheadLimiter
{
public:
    void prepare (double sampleRate, int numChannels, double lookaheadMs = 5.0)
    {
        fs = sampleRate;
        channels = juce::jmax (1, numChannels);
        window = juce::jmax (2, (int) std::round (fs * lookaheadMs * 0.001));

        delayLines.assign ((size_t) channels, std::vector<float> ((size_t) window, 0.0f));
        dequeIndex.assign ((size_t) window + 1, 0);
        dequeValue.assign ((size_t) window + 1, 1.0f);
        averageHistory.assign ((size_t) window, 1.0);
        reset();
    }

    void reset()
    {
        for (auto& d : delayLines)
            std::fill (d.begin(), d.end(), 0.0f);

        std::fill (averageHistory.begin(), averageHistory.end(), 1.0);
        averageSum = (double) window;
        averagePos = 0;
        writePos = 0;
        sampleCounter = 0;
        dequeHead = dequeTail = 0;
        releaseGain = 1.0;
        lastGain = 1.0f;
        minGainSinceRead = 1.0f;
    }

    int getLatencySamples() const noexcept { return window - 1; }

    /** ceilingDb: output ceiling in dBFS, releaseMs: release time. */
    void process (juce::AudioBuffer<float>& buffer, float ceilingDb, float releaseMs)
    {
        const float ceiling = juce::Decibels::decibelsToGain (ceilingDb);
        const double releaseCoef = 1.0 - std::exp (-1.0 / (fs * juce::jmax (1.0f, releaseMs) * 0.001));
        const int numSamples = buffer.getNumSamples();
        const int numCh = juce::jmin (channels, buffer.getNumChannels());
        const int delay = window - 1;

        for (int i = 0; i < numSamples; ++i)
        {
            // 1. required gain for the incoming sample
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                peak = juce::jmax (peak, std::abs (buffer.getSample (ch, i)));

            const float required = peak > ceiling ? ceiling / peak : 1.0f;

            // 2. sliding minimum over the last `window` samples
            pushMin (sampleCounter, required);
            const float heldMin = dequeValue[(size_t) dequeHead];

            // 3. release: drop instantly, recover exponentially, never exceed heldMin
            if (heldMin < releaseGain)
                releaseGain = heldMin;
            else
                releaseGain += (heldMin - releaseGain) * releaseCoef;

            // 4. moving average over the window
            averageSum += releaseGain - averageHistory[(size_t) averagePos];
            averageHistory[(size_t) averagePos] = releaseGain;
            averagePos = (averagePos + 1) % window;
            const float gain = (float) juce::jmin (1.0, averageSum / (double) window);

            // delay the audio and apply gain
            const int readPos = (writePos - delay + window) % window;
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& line = delayLines[(size_t) ch];
                line[(size_t) writePos] = buffer.getSample (ch, i);
                const float out = line[(size_t) readPos] * gain;
                buffer.setSample (ch, i, juce::jlimit (-ceiling, ceiling, out));
            }

            writePos = (writePos + 1) % window;
            ++sampleCounter;
            lastGain = gain;
            minGainSinceRead = juce::jmin (minGainSinceRead, gain);
        }
    }

    /** Largest gain reduction (in dB, positive number) since the last call. Audio thread only. */
    float takeMaxGainReductionDb() noexcept
    {
        const float gr = -juce::Decibels::gainToDecibels (minGainSinceRead, -60.0f);
        minGainSinceRead = 1.0f;
        return gr;
    }

private:
    void pushMin (long long index, float value)
    {
        const int cap = window + 1;

        // drop larger-or-equal values from the back
        while (dequeHead != dequeTail)
        {
            const int back = (dequeTail - 1 + cap) % cap;
            if (dequeValue[(size_t) back] >= value)
                dequeTail = back;
            else
                break;
        }

        dequeIndex[(size_t) dequeTail] = index;
        dequeValue[(size_t) dequeTail] = value;
        dequeTail = (dequeTail + 1) % cap;

        // drop values that have left the window from the front
        while (dequeIndex[(size_t) dequeHead] <= index - window)
            dequeHead = (dequeHead + 1) % cap;
    }

    double fs = 44100.0;
    int channels = 2;
    int window = 220;

    std::vector<std::vector<float>> delayLines;
    int writePos = 0;
    long long sampleCounter = 0;

    std::vector<long long> dequeIndex;
    std::vector<float> dequeValue;
    int dequeHead = 0, dequeTail = 0;

    double releaseGain = 1.0;

    std::vector<double> averageHistory;
    double averageSum = 0.0;
    int averagePos = 0;

    float lastGain = 1.0f, minGainSinceRead = 1.0f;
};
