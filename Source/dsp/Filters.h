#pragma once

#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace riff
{
using Biquad = juce::dsp::IIR::Filter<float>;
using BiquadCoefs = juce::dsp::IIR::ArrayCoefficients<float>;

// Assigns second-order coefficients in place (no allocation once the filter has been prepared).
inline void setCoefficients (Biquad& filter, const std::array<float, 6>& c) noexcept
{
    *filter.coefficients = c;
}

inline std::array<float, 6> flat (double sampleRate)
{
    return BiquadCoefs::makePeakFilter (sampleRate, 1000.0f, 0.7f, 1.0f);
}

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

// One-pole low-pass, used inside the oversampled amp where biquads would be overkill.
struct OnePoleLowPass
{
    void setCutoff (double sampleRate, double hz) noexcept
    {
        a = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * hz / sampleRate));
    }

    float process (float x) noexcept
    {
        z += a * (x - z);
        return z;
    }

    void reset() noexcept { z = 0.0f; }

    float a = 1.0f, z = 0.0f;
};

struct OnePoleHighPass
{
    void setCutoff (double sampleRate, double hz) noexcept { lp.setCutoff (sampleRate, hz); }
    float process (float x) noexcept { return x - lp.process (x); }
    void reset() noexcept { lp.reset(); }

    OnePoleLowPass lp;
};

// Removes the DC offset produced by asymmetric clipping.
struct DCBlocker
{
    void prepare (double sampleRate) noexcept
    {
        r = (float) std::exp (-juce::MathConstants<double>::twoPi * 10.0 / sampleRate);
    }

    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    void reset() noexcept { x1 = y1 = 0.0f; }

    float r = 0.999f, x1 = 0.0f, y1 = 0.0f;
};
} // namespace riff
