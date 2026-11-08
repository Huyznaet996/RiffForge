#pragma once

#include "Filters.h"
#include "../Parameters.h"
#include <atomic>
#include <vector>

namespace riff
{
// A speaker cabinet modelled with a cascade of biquads: low-end resonance, box scoop,
// speaker presence peak and the steep high-frequency roll-off of a guitar speaker.
class FilterCabinet
{
public:
    void prepare (double fs, CabModel model)
    {
        numStages = 0;
        auto add = [&] (const std::array<float, 6>& c)
        {
            setCoefficients (stages[(size_t) numStages], c);
            stages[(size_t) numStages].reset();
            ++numStages;
        };

        if (model == CabModel::combo1x12)
        {
            // Open-back 1x12: lighter low end, airy top
            add (BiquadCoefs::makeHighPass   (fs,   85.0f, 0.70f));
            add (BiquadCoefs::makePeakFilter (fs,  125.0f, 1.00f, dbToGain ( 3.0f)));
            add (BiquadCoefs::makePeakFilter (fs,  520.0f, 1.20f, dbToGain (-3.0f)));
            add (BiquadCoefs::makePeakFilter (fs, 2100.0f, 1.60f, dbToGain ( 3.5f)));
            add (BiquadCoefs::makeLowPass    (fs, 5600.0f, 0.70f));
            add (BiquadCoefs::makeLowPass    (fs, 7200.0f, 0.55f));
        }
        else
        {
            // Closed-back 4x12: tight thump, scooped low-mids, focused upper mids
            add (BiquadCoefs::makeHighPass   (fs,   70.0f, 0.80f));
            add (BiquadCoefs::makePeakFilter (fs,  105.0f, 1.10f, dbToGain ( 5.0f)));
            add (BiquadCoefs::makePeakFilter (fs,  420.0f, 1.00f, dbToGain (-4.0f)));
            add (BiquadCoefs::makePeakFilter (fs, 1600.0f, 1.20f, dbToGain ( 2.0f)));
            add (BiquadCoefs::makePeakFilter (fs, 3200.0f, 2.50f, dbToGain ( 3.0f)));
            add (BiquadCoefs::makeLowPass    (fs, 4800.0f, 0.70f));
            add (BiquadCoefs::makeLowPass    (fs, 6000.0f, 0.55f));
        }
    }

    void reset()
    {
        for (int i = 0; i < numStages; ++i)
            stages[(size_t) i].reset();
    }

    void process (float* data, int n) noexcept
    {
        for (int s = 0; s < numStages; ++s)
        {
            auto& f = stages[(size_t) s];
            for (int i = 0; i < n; ++i)
                data[i] = f.processSample (data[i]);
        }
    }

private:
    std::array<Biquad, 8> stages;
    int numStages = 0;
};

// Cabinet section: bypass, two built-in cabinets or a user impulse response (convolution).
// Switching between them crossfades so there are no clicks.
class CabSimulator
{
public:
    enum Path { bypass = 0, combo, stack, impulse };

    // Impulse responses are scaled to this energy so they sit at roughly the same level
    // as the built-in cabinets (measured with the 4x12 model, see tests/RenderTest.cpp).
    static constexpr float irTargetEnergy = 0.20f;

    void prepare (double sampleRate, int maxBlockSize)
    {
        fs = sampleRate;
        maxBlock = juce::jmax (1, maxBlockSize);
        scratch.assign ((size_t) maxBlock, 0.0f);
        comboCab.prepare (fs, CabModel::combo1x12);
        stackCab.prepare (fs, CabModel::stack4x12);
        convolution.prepare ({ fs, (juce::uint32) maxBlock, 1 });
        fadeLength = juce::jmax (1, (int) (0.03 * fs));
        reset();
    }

    void reset()
    {
        comboCab.reset();
        stackCab.reset();
        convolution.reset();
        fadeRemaining = 0;
    }

    // Message thread. Takes ownership of a mono IR; the convolution engine swaps it in
    // on a background thread and crossfades internally.
    void loadImpulseResponse (juce::AudioBuffer<float>&& ir, double irSampleRate)
    {
        convolution.loadImpulseResponse (std::move (ir), irSampleRate,
                                         juce::dsp::Convolution::Stereo::no,
                                         juce::dsp::Convolution::Trim::no,
                                         juce::dsp::Convolution::Normalise::no);
        irReady = true;
    }

    void clearImpulseResponse() noexcept { irReady = false; }
    bool hasImpulseResponse() const noexcept { return irReady; }

    // Audio thread. numSamples must not exceed the prepared block size.
    void process (float* data, int numSamples, int requestedPath) noexcept
    {
        int target = juce::jlimit (0, 3, requestedPath);
        if (target == impulse && ! irReady)
            target = stack;

        if (target != currentPath)
        {
            previousPath = currentPath;
            currentPath = target;
            fadeRemaining = fadeLength;
            resetPath (currentPath);
        }

        if (fadeRemaining <= 0)
        {
            runPath (currentPath, data, numSamples);
            return;
        }

        std::copy (data, data + numSamples, scratch.begin());
        runPath (previousPath, scratch.data(), numSamples);
        runPath (currentPath, data, numSamples);

        for (int i = 0; i < numSamples && fadeRemaining > 0; ++i, --fadeRemaining)
        {
            const float t = 1.0f - (float) fadeRemaining / (float) fadeLength;
            data[i] = scratch[(size_t) i] + t * (data[i] - scratch[(size_t) i]);
        }
    }

private:
    void resetPath (int path)
    {
        if (path == combo)        comboCab.reset();
        else if (path == stack)   stackCab.reset();
        else if (path == impulse) convolution.reset();
    }

    void runPath (int path, float* data, int n) noexcept
    {
        switch (path)
        {
            case combo:   comboCab.process (data, n); break;
            case stack:   stackCab.process (data, n); break;
            case impulse:
            {
                float* channels[] = { data };
                juce::dsp::AudioBlock<float> block (channels, 1, (size_t) n);
                convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
                break;
            }
            default: break; // bypass
        }
    }

    double fs = 44100.0;
    int maxBlock = 512;
    FilterCabinet comboCab, stackCab;
    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 256 } };
    std::atomic<bool> irReady { false };
    int currentPath = stack, previousPath = stack;
    int fadeRemaining = 0, fadeLength = 1;
    std::vector<float> scratch;
};
} // namespace riff
