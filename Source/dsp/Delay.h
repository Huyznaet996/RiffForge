#pragma once

#include "Filters.h"
#include <vector>

namespace riff
{
// Analog-flavoured echo: smoothed delay time (tape-like when you turn the knob),
// darker repeats via a low-pass in the feedback loop, "trails" when switched off.
class Delay
{
public:
    static constexpr double maxDelaySeconds = 2.0;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        buffer.assign ((size_t) (maxDelaySeconds * fs) + 4, 0.0f);
        timeCoef = (float) std::exp (-1.0 / (0.12 * fs));
        feedbackLowPass.setCutoff (fs, 3800.0);
        feedbackHighPass.setCutoff (fs, 90.0);
        wet.reset (fs, 0.05);
        feedback.reset (fs, 0.05);
        reset();
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
        feedbackLowPass.reset();
        feedbackHighPass.reset();
        currentDelay = targetDelay;
    }

    void setParameters (bool enabled, float timeMs, float feedbackAmount, float mix) noexcept
    {
        targetDelay = juce::jlimit (1.0f, (float) buffer.size() - 3.0f, timeMs * 0.001f * (float) fs);
        feedback.setTargetValue (juce::jlimit (0.0f, 0.95f, feedbackAmount));
        wet.setTargetValue (enabled ? mix : 0.0f);
    }

    void process (float* data, int numSamples) noexcept
    {
        const int size = (int) buffer.size();

        for (int i = 0; i < numSamples; ++i)
        {
            currentDelay = targetDelay + (currentDelay - targetDelay) * timeCoef;

            float readPos = (float) writePos - currentDelay;
            if (readPos < 0.0f)
                readPos += (float) size;

            const int i0 = (int) readPos;
            const int i1 = (i0 + 1) % size;
            const float frac = readPos - (float) i0;
            const float delayed = buffer[(size_t) i0] + frac * (buffer[(size_t) i1] - buffer[(size_t) i0]);

            // tanh: repeats saturate like an analog echo instead of piling up at high feedback
            const float fb = std::tanh (feedbackHighPass.process (feedbackLowPass.process (delayed)) * feedback.getNextValue());
            const float dry = data[i];
            buffer[(size_t) writePos] = dry + fb;

            if (++writePos >= size)
                writePos = 0;

            data[i] = dry + delayed * wet.getNextValue();
        }
    }

private:
    double fs = 44100.0;
    std::vector<float> buffer;
    int writePos = 0;
    float currentDelay = 1000.0f, targetDelay = 1000.0f, timeCoef = 0.999f;
    OnePoleLowPass feedbackLowPass;
    OnePoleHighPass feedbackHighPass;
    juce::SmoothedValue<float> wet { 0.0f }, feedback { 0.0f };
};
} // namespace riff
