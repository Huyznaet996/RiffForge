#pragma once

#include <cmath>
#include <algorithm>

namespace riff
{
// Noise gate with hysteresis and hold. Works sample by sample on the clean input,
// before the amp, so the noise the drive stage would amplify gets cut at the source.
class NoiseGate
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        envAttack   = coef (0.0005);
        envRelease  = coef (0.030);
        gainAttack  = coef (0.001);
        gainRelease = coef (0.080);
        holdSamples = (int) (0.050 * fs);
        reset();
    }

    void reset() noexcept
    {
        env = 0.0f;
        gain = 1.0f;
        open = true;
        holdCounter = 0;
    }

    void setThresholdDecibels (float thresholdDb) noexcept
    {
        openThreshold  = std::pow (10.0f, thresholdDb / 20.0f);
        closeThreshold = openThreshold * 0.5f; // 6 dB hysteresis
    }

    void setEnabled (bool shouldBeEnabled) noexcept { enabled = shouldBeEnabled; }

    float processSample (float x) noexcept
    {
        const float level = std::abs (x);
        env = level > env ? level + (env - level) * envAttack
                          : level + (env - level) * envRelease;

        if (env > openThreshold)
        {
            open = true;
            holdCounter = holdSamples;
        }
        else if (env < closeThreshold)
        {
            if (holdCounter > 0) --holdCounter;
            else                 open = false;
        }

        const float target = (! enabled || open) ? 1.0f : 0.0f;
        gain = target > gain ? target + (gain - target) * gainAttack
                             : target + (gain - target) * gainRelease;

        return x * gain;
    }

    float getCurrentGain() const noexcept { return gain; }

private:
    float coef (double seconds) const { return (float) std::exp (-1.0 / (seconds * fs)); }

    double fs = 44100.0;
    float envAttack = 0.0f, envRelease = 0.0f, gainAttack = 0.0f, gainRelease = 0.0f;
    float openThreshold = 0.001f, closeThreshold = 0.0005f;
    float env = 0.0f, gain = 1.0f;
    bool open = true, enabled = true;
    int holdSamples = 0, holdCounter = 0;
};
} // namespace riff
