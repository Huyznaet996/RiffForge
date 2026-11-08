#pragma once

#include "Filters.h"
#include "../Parameters.h"

namespace riff
{
// Preamp: pre-emphasis EQ -> 4x oversampled asymmetric tube-style clipping -> DC blocker.
// The "Lead" channel cascades two clipping stages for high gain.
class AmpStage
{
public:
    struct Voicing
    {
        float preHighPassHz;   // tightens the low end before clipping
        float midHz, midDb, midQ;
        float minGainDb, maxGainDb;
        float bias;            // asymmetry -> even harmonics
        bool  twoStage;
        float stage2GainDb;
        float postLowPassHz;   // tames fizz above the cabinet range
        float trim;            // level matching between channels
    };

    static const Voicing& voicingFor (AmpChannel ch) noexcept
    {
        static const Voicing voicings[3] =
        {
            /* clean  */ {  45.0f,  700.0f, 0.0f, 0.70f,  0.0f, 22.0f, 0.10f, false,  0.0f, 9500.0f, 1.00f },
            /* crunch */ {  90.0f,  900.0f, 4.0f, 0.70f,  6.0f, 36.0f, 0.18f, false,  0.0f, 7500.0f, 0.80f },
            /* lead   */ { 150.0f,  800.0f, 6.0f, 0.80f, 12.0f, 40.0f, 0.22f, true,  12.0f, 6500.0f, 0.62f },
        };
        return voicings[juce::jlimit (0, 2, (int) ch)];
    }

    void prepare (double sampleRate, int maxBlockSize)
    {
        fs = sampleRate;
        maxBlock = juce::jmax (1, maxBlockSize);

        oversampling.initProcessing ((size_t) maxBlock);
        osRate = fs * (double) oversampling.getOversamplingFactor();

        setCoefficients (preHighPass, flat (fs));
        setCoefficients (preMid, flat (fs));
        preHighPass.reset();
        preMid.reset();

        stageGain.reset (osRate, 0.05);
        makeup.reset (osRate, 0.05);

        dcBlocker.prepare (fs);
        interStageHighPass.setCutoff (osRate, 120.0);
        interStageLowPass.setCutoff (osRate, 5500.0);

        applyVoicing (true);
        reset();
    }

    void reset()
    {
        oversampling.reset();
        preHighPass.reset();
        preMid.reset();
        interStageHighPass.reset();
        interStageLowPass.reset();
        postLowPass.reset();
        dcBlocker.reset();
    }

    void setChannel (AmpChannel ch) noexcept
    {
        if (ch != channel)
        {
            channel = ch;
            applyVoicing (false);
        }
    }

    void setDrive (float drive0to10) noexcept
    {
        if (! juce::approximatelyEqual (drive0to10, drive))
        {
            drive = drive0to10;
            updateGains (false);
        }
    }

    int getLatencySamples() const noexcept
    {
        return (int) std::round (oversampling.getLatencyInSamples());
    }

    void process (float* data, int numSamples) noexcept
    {
        for (int start = 0; start < numSamples; start += maxBlock)
            processChunk (data + start, juce::jmin (maxBlock, numSamples - start));
    }

private:
    static float shape (float x, float bias) noexcept
    {
        return std::tanh (x + bias) - std::tanh (bias);
    }

    void processChunk (float* data, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
            data[i] = preMid.processSample (preHighPass.processSample (data[i]));

        float* channels[] = { data };
        juce::dsp::AudioBlock<float> block (channels, 1, (size_t) n);
        auto up = oversampling.processSamplesUp (block);

        auto* x = up.getChannelPointer (0);
        const auto numUp = (int) up.getNumSamples();
        const auto& v = voicingFor (channel);
        const float stage2Gain = dbToGain (v.stage2GainDb);

        for (int i = 0; i < numUp; ++i)
        {
            float s = shape (x[i] * stageGain.getNextValue(), v.bias);

            if (v.twoStage)
            {
                s = interStageLowPass.process (interStageHighPass.process (s));
                s = shape (s * stage2Gain, v.bias * 0.5f);
            }

            x[i] = postLowPass.process (s) * makeup.getNextValue();
        }

        oversampling.processSamplesDown (block);

        for (int i = 0; i < n; ++i)
            data[i] = dcBlocker.process (data[i]);
    }

    void applyVoicing (bool force) noexcept
    {
        const auto& v = voicingFor (channel);
        setCoefficients (preHighPass, BiquadCoefs::makeHighPass (fs, v.preHighPassHz, 0.707f));
        setCoefficients (preMid, BiquadCoefs::makePeakFilter (fs, v.midHz, v.midQ, dbToGain (v.midDb)));
        postLowPass.setCutoff (osRate, v.postLowPassHz);
        updateGains (force);
    }

    void updateGains (bool force) noexcept
    {
        const auto& v = voicingFor (channel);
        const float d = juce::jlimit (0.0f, 1.0f, drive / 10.0f);
        const float gain = dbToGain (juce::jmap (d, v.minGainDb, v.maxGainDb));

        // Loudness compensation: estimate how hard a typical guitar level (~ -12 dBFS) drives the
        // clipper and scale the output back, so turning up Drive adds distortion, not just volume.
        constexpr float typicalInput = 0.25f;
        const float level = juce::jmax (0.05f, std::tanh (gain * typicalInput));
        const float comp = v.trim * 0.5f / level;

        if (force)
        {
            stageGain.setCurrentAndTargetValue (gain);
            makeup.setCurrentAndTargetValue (comp);
        }
        else
        {
            stageGain.setTargetValue (gain);
            makeup.setTargetValue (comp);
        }
    }

    juce::dsp::Oversampling<float> oversampling { 1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };

    double fs = 44100.0, osRate = 176400.0;
    int maxBlock = 512;
    AmpChannel channel = AmpChannel::crunch;
    float drive = 5.0f;

    Biquad preHighPass, preMid;
    OnePoleHighPass interStageHighPass;
    OnePoleLowPass interStageLowPass, postLowPass;
    DCBlocker dcBlocker;
    juce::SmoothedValue<float> stageGain { 1.0f }, makeup { 1.0f };
};
} // namespace riff
