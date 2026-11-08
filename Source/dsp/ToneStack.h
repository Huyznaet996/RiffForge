#pragma once

#include "Filters.h"

namespace riff
{
// Bass / Middle / Treble / Presence, each +-12 dB.
class ToneStack
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto* f : { &bass, &mid, &treble, &presence })
        {
            setCoefficients (*f, flat (fs));
            f->reset();
        }
        lastBass = lastMid = lastTreble = lastPresence = 1000.0f; // force an update
        setGains (0.0f, 0.0f, 0.0f, 0.0f);
    }

    void reset()
    {
        for (auto* f : { &bass, &mid, &treble, &presence })
            f->reset();
    }

    void setGains (float bassDb, float midDb, float trebleDb, float presenceDb) noexcept
    {
        if (! juce::approximatelyEqual (bassDb, lastBass))
            setCoefficients (bass, BiquadCoefs::makeLowShelf (fs, 110.0f, 0.6f, dbToGain (lastBass = bassDb)));

        if (! juce::approximatelyEqual (midDb, lastMid))
            setCoefficients (mid, BiquadCoefs::makePeakFilter (fs, 650.0f, 0.75f, dbToGain (lastMid = midDb)));

        if (! juce::approximatelyEqual (trebleDb, lastTreble))
            setCoefficients (treble, BiquadCoefs::makeHighShelf (fs, 2600.0f, 0.6f, dbToGain (lastTreble = trebleDb)));

        if (! juce::approximatelyEqual (presenceDb, lastPresence))
            setCoefficients (presence, BiquadCoefs::makePeakFilter (fs, 4800.0f, 0.9f, dbToGain (lastPresence = presenceDb)));
    }

    void process (float* data, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            data[i] = presence.processSample (treble.processSample (mid.processSample (bass.processSample (data[i]))));
    }

private:
    double fs = 44100.0;
    Biquad bass, mid, treble, presence;
    float lastBass = 0.0f, lastMid = 0.0f, lastTreble = 0.0f, lastPresence = 0.0f;
};
} // namespace riff
