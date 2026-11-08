#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

namespace riff
{
// Single-producer / single-consumer sample FIFO: the audio thread pushes the guitar
// signal, the editor pulls it for the tuner. Drops samples when full (editor closed).
class SampleFifo
{
public:
    explicit SampleFifo (int capacity = 1 << 15) : fifo (capacity), data ((size_t) capacity, 0.0f) {}

    void push (const float* samples, int numSamples) noexcept
    {
        const auto scope = fifo.write (juce::jmin (numSamples, fifo.getFreeSpace()));
        if (scope.blockSize1 > 0) std::copy (samples, samples + scope.blockSize1, data.begin() + scope.startIndex1);
        if (scope.blockSize2 > 0) std::copy (samples + scope.blockSize1, samples + scope.blockSize1 + scope.blockSize2, data.begin() + scope.startIndex2);
    }

    template <typename Fn>
    void pull (Fn&& consume)
    {
        const auto scope = fifo.read (fifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i) consume (data[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i) consume (data[(size_t) (scope.startIndex2 + i)]);
    }

private:
    juce::AbstractFifo fifo;
    std::vector<float> data;
};

// YIN pitch detector running on a low-passed, decimated copy of the input (~24 kHz),
// covering 55 Hz (low A on a 5-string bass / drop tunings) up to ~1.2 kHz.
class PitchDetector
{
public:
    struct Result
    {
        bool  valid = false;
        float frequency = 0.0f;
        int   midiNote = 0;
        float cents = 0.0f;
        juce::String noteName;
    };

    void setSampleRate (double sampleRate)
    {
        if (juce::approximatelyEqual (sampleRate, inputRate))
            return;

        inputRate = sampleRate;
        decimation = juce::jmax (1, (int) std::round (sampleRate / 24000.0));
        rate = sampleRate / decimation;

        const double a = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 2500.0 / sampleRate);
        lpCoef = (float) a;

        minLag = juce::jmax (2, (int) (rate / 1200.0));
        maxLag = (int) (rate / 55.0) + 2;
        window = 1024;
        history.assign ((size_t) (window + maxLag + 2), 0.0f);
        diff.assign ((size_t) maxLag + 2, 0.0f);
        writePos = 0;
        filled = 0;
        counter = 0;
    }

    void pushSample (float x) noexcept
    {
        lp1 += lpCoef * (x - lp1);
        lp2 += lpCoef * (lp1 - lp2);

        if (++counter < decimation)
            return;

        counter = 0;
        history[(size_t) writePos] = lp2;
        writePos = (writePos + 1) % (int) history.size();
        filled = juce::jmin (filled + 1, (int) history.size());
    }

    Result analyse()
    {
        Result r;
        const int n = (int) history.size();
        if (filled < n)
            return r;

        // Unroll the circular history, oldest first.
        frame.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            frame[(size_t) i] = history[(size_t) ((writePos + i) % n)];

        double energy = 0.0;
        for (int i = 0; i < window; ++i)
            energy += (double) frame[(size_t) i] * frame[(size_t) i];

        if (std::sqrt (energy / window) < 0.003) // about -50 dBFS: too quiet to tune
            return r;

        // Cumulative mean normalised difference function.
        diff[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double d = 0.0;
            const float* a = frame.data();
            const float* b = frame.data() + tau;
            for (int j = 0; j < window; ++j)
            {
                const float delta = a[j] - b[j];
                d += (double) delta * delta;
            }
            running += d;
            diff[(size_t) tau] = running > 0.0 ? (float) (d * tau / running) : 1.0f;
        }

        int best = -1;
        for (int tau = minLag; tau < maxLag; ++tau)
        {
            if (diff[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < maxLag && diff[(size_t) tau + 1] < diff[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }

        if (best < 0)
            return r;

        // Parabolic interpolation around the minimum.
        const float y0 = diff[(size_t) best - 1], y1 = diff[(size_t) best], y2 = diff[(size_t) best + 1];
        const float denom = y0 - 2.0f * y1 + y2;
        const float shift = std::abs (denom) > 1.0e-9f ? 0.5f * (y0 - y2) / denom : 0.0f;
        const float period = (float) best + juce::jlimit (-1.0f, 1.0f, shift);

        r.frequency = (float) (rate / period);
        const float midi = 69.0f + 12.0f * std::log2 (r.frequency / 440.0f);
        r.midiNote = (int) std::round (midi);
        r.cents = (midi - (float) r.midiNote) * 100.0f;
        r.noteName = juce::MidiMessage::getMidiNoteName (r.midiNote, true, true, 4);
        r.valid = true;
        return r;
    }

private:
    double inputRate = 0.0, rate = 24000.0;
    int decimation = 2, counter = 0;
    float lpCoef = 0.5f, lp1 = 0.0f, lp2 = 0.0f;
    int minLag = 20, maxLag = 440, window = 1024;
    std::vector<float> history, diff, frame;
    int writePos = 0, filled = 0;
};
} // namespace riff
