#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

/**
    ITU-R BS.1770-4 / EBU R128 loudness meter.

    - K-weighting (pre-filter shelf + RLB high-pass), computed for any sample rate
    - Momentary (400 ms), short-term (3 s) and gated integrated loudness
    - Integrated loudness uses a fixed-size histogram, so nothing is allocated
      on the audio thread.

    process() is called from the audio thread. The get...() functions read
    atomics and are safe to call from the UI thread.
*/
class LoudnessMeter
{
public:
    static constexpr float silenceLufs = -100.0f;

    void prepare (double sampleRate, int numChannels)
    {
        fs = sampleRate;
        channels = juce::jmax (1, numChannels);
        hopSize = juce::jmax (1, (int) std::round (fs * 0.1)); // 100 ms hops
        filters.assign ((size_t) channels, {});
        designKWeighting();
        reset();
    }

    void reset()
    {
        for (auto& ch : filters)
            for (auto& s : ch)
                s.z1 = s.z2 = 0.0;

        hopEnergies.fill (0.0);
        hopIndex = 0;
        hopsFilled = 0;
        hopAccumulator = 0.0;
        samplesInHop = 0;
        histogramCount.fill (0);
        histogramEnergy.fill (0.0);

        momentary.store (silenceLufs);
        shortTerm.store (silenceLufs);
        integrated.store (silenceLufs);
    }

    void process (const juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numCh = juce::jmin (channels, buffer.getNumChannels());

        for (int i = 0; i < numSamples; ++i)
        {
            double sumSquares = 0.0;

            for (int ch = 0; ch < numCh; ++ch)
            {
                double x = buffer.getSample (ch, i);
                for (auto& stage : filters[(size_t) ch])
                    x = stage.process (x);
                sumSquares += x * x; // channel weight 1.0 for L and R
            }

            hopAccumulator += sumSquares;

            if (++samplesInHop >= hopSize)
                finishHop();
        }
    }

    float getMomentaryLufs() const   { return momentary.load(); }
    float getShortTermLufs() const   { return shortTerm.load(); }
    float getIntegratedLufs() const  { return integrated.load(); }

    static double energyToLufs (double meanSquare)
    {
        return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10 (meanSquare) : (double) silenceLufs;
    }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;

        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    // K-weighting coefficients for arbitrary sample rates (same derivation as libebur128).
    void designKWeighting()
    {
        const double pi = juce::MathConstants<double>::pi;

        // Stage 1: high-frequency shelving filter (+4 dB above ~1.5 kHz)
        Biquad shelf;
        {
            const double f0 = 1681.974450955533;
            const double G  = 3.999843853973347;
            const double Q  = 0.7071752369554196;
            const double K  = std::tan (pi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;

            shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
            shelf.b1 = 2.0 * (K * K - Vh) / a0;
            shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
            shelf.a1 = 2.0 * (K * K - 1.0) / a0;
            shelf.a2 = (1.0 - K / Q + K * K) / a0;
        }

        // Stage 2: RLB high-pass (~38 Hz)
        Biquad highPass;
        {
            const double f0 = 38.13547087602444;
            const double Q  = 0.5003270373238773;
            const double K  = std::tan (pi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;

            highPass.b0 = 1.0;
            highPass.b1 = -2.0;
            highPass.b2 = 1.0;
            highPass.a1 = 2.0 * (K * K - 1.0) / a0;
            highPass.a2 = (1.0 - K / Q + K * K) / a0;
        }

        for (auto& ch : filters)
            ch = { shelf, highPass };
    }

    void finishHop()
    {
        hopEnergies[(size_t) hopIndex] = hopAccumulator / (double) samplesInHop;
        hopIndex = (hopIndex + 1) % numHopsShortTerm;
        hopsFilled = juce::jmin (hopsFilled + 1, numHopsShortTerm);
        hopAccumulator = 0.0;
        samplesInHop = 0;

        // Momentary: last 4 hops (400 ms, 75 % overlap)
        if (hopsFilled >= numHopsMomentary)
        {
            const double blockEnergy = meanOfLastHops (numHopsMomentary);
            const double blockLufs = energyToLufs (blockEnergy);
            momentary.store ((float) blockLufs);
            addToHistogram (blockLufs, blockEnergy);
            integrated.store ((float) computeIntegrated());
        }

        // Short-term: last 30 hops (3 s)
        if (hopsFilled >= numHopsShortTerm)
            shortTerm.store ((float) energyToLufs (meanOfLastHops (numHopsShortTerm)));
    }

    double meanOfLastHops (int count) const
    {
        double sum = 0.0;
        for (int k = 1; k <= count; ++k)
            sum += hopEnergies[(size_t) ((hopIndex - k + numHopsShortTerm) % numHopsShortTerm)];
        return sum / (double) count;
    }

    // Histogram from -70 LUFS to +10 LUFS in 0.1 LU bins.
    static constexpr double histMin = -70.0, histMax = 10.0, histStep = 0.1;
    static constexpr int numBins = 800;

    void addToHistogram (double lufs, double energy)
    {
        if (lufs <= histMin) // absolute gate
            return;

        const int bin = juce::jlimit (0, numBins - 1, (int) ((lufs - histMin) / histStep));
        ++histogramCount[(size_t) bin];
        histogramEnergy[(size_t) bin] += energy;
    }

    double gatedMeanAbove (int firstBin) const
    {
        double energy = 0.0;
        long long count = 0;
        for (int b = firstBin; b < numBins; ++b)
        {
            energy += histogramEnergy[(size_t) b];
            count += histogramCount[(size_t) b];
        }
        return count > 0 ? energy / (double) count : 0.0;
    }

    double computeIntegrated() const
    {
        const double absGated = gatedMeanAbove (0);
        if (absGated <= 0.0)
            return silenceLufs;

        const double relativeGate = energyToLufs (absGated) - 10.0;
        const int firstBin = juce::jlimit (0, numBins - 1,
                                           (int) std::ceil ((relativeGate - histMin) / histStep));
        return energyToLufs (gatedMeanAbove (firstBin));
    }

    static constexpr int numHopsMomentary = 4;
    static constexpr int numHopsShortTerm = 30;

    double fs = 44100.0;
    int channels = 2;
    int hopSize = 4410;

    std::vector<std::array<Biquad, 2>> filters;

    std::array<double, numHopsShortTerm> hopEnergies {};
    int hopIndex = 0, hopsFilled = 0;
    double hopAccumulator = 0.0;
    int samplesInHop = 0;

    std::array<long long, numBins> histogramCount {};
    std::array<double, numBins> histogramEnergy {};

    std::atomic<float> momentary { silenceLufs }, shortTerm { silenceLufs }, integrated { silenceLufs };
};
