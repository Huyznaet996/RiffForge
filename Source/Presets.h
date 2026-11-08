#pragma once

#include "Parameters.h"
#include <vector>
#include <utility>

namespace riff
{
struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values; // parameter ID -> real (denormalised) value
};

// Parameters a preset leaves out keep their default value.
inline const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> presets
    {
        { "Init", {} },

        { "Clean Sparkle", {
            { ID::channel, 0 }, { ID::drive, 3.0f }, { ID::inputGain, 0.0f },
            { ID::gateOn, 1 }, { ID::gateThreshold, -66.0f },
            { ID::bass, 2.0f }, { ID::mid, -1.0f }, { ID::treble, 3.0f }, { ID::presence, 2.5f },
            { ID::cabOn, 1 }, { ID::cabModel, 0 },
            { ID::delayOn, 0 },
            { ID::reverbOn, 1 }, { ID::reverbSize, 0.55f }, { ID::reverbMix, 0.22f },
            { ID::outputLevel, -3.0f } } },

        { "Blues Crunch", {
            { ID::channel, 1 }, { ID::drive, 4.5f }, { ID::inputGain, 0.0f },
            { ID::gateOn, 1 }, { ID::gateThreshold, -66.0f },
            { ID::bass, 1.0f }, { ID::mid, 2.0f }, { ID::treble, 1.0f }, { ID::presence, 0.0f },
            { ID::cabOn, 1 }, { ID::cabModel, 0 },
            { ID::delayOn, 1 }, { ID::delayTime, 380.0f }, { ID::delayFeedback, 0.22f }, { ID::delayMix, 0.15f },
            { ID::reverbOn, 1 }, { ID::reverbSize, 0.4f }, { ID::reverbMix, 0.15f },
            { ID::outputLevel, -1.5f } } },

        { "Classic Rock", {
            { ID::channel, 1 }, { ID::drive, 7.5f }, { ID::inputGain, 0.0f },
            { ID::gateOn, 1 }, { ID::gateThreshold, -62.0f },
            { ID::bass, 1.5f }, { ID::mid, 3.0f }, { ID::treble, 2.0f }, { ID::presence, 1.5f },
            { ID::cabOn, 1 }, { ID::cabModel, 1 },
            { ID::delayOn, 0 },
            { ID::reverbOn, 1 }, { ID::reverbSize, 0.35f }, { ID::reverbMix, 0.12f },
            { ID::outputLevel, -5.0f } } },

        { "Modern Metal", {
            { ID::channel, 2 }, { ID::drive, 7.0f }, { ID::inputGain, 0.0f },
            { ID::gateOn, 1 }, { ID::gateThreshold, -52.0f },
            { ID::bass, 3.0f }, { ID::mid, -4.0f }, { ID::treble, 2.0f }, { ID::presence, 3.0f },
            { ID::cabOn, 1 }, { ID::cabModel, 1 },
            { ID::delayOn, 0 },
            { ID::reverbOn, 1 }, { ID::reverbSize, 0.3f }, { ID::reverbMix, 0.08f },
            { ID::outputLevel, -1.0f } } },

        { "Lead Solo", {
            { ID::channel, 2 }, { ID::drive, 6.0f }, { ID::inputGain, 0.0f },
            { ID::gateOn, 1 }, { ID::gateThreshold, -58.0f },
            { ID::bass, 0.0f }, { ID::mid, 3.5f }, { ID::treble, 1.0f }, { ID::presence, 1.0f },
            { ID::cabOn, 1 }, { ID::cabModel, 1 },
            { ID::delayOn, 1 }, { ID::delayTime, 450.0f }, { ID::delayFeedback, 0.35f }, { ID::delayMix, 0.25f },
            { ID::reverbOn, 1 }, { ID::reverbSize, 0.6f }, { ID::reverbMix, 0.25f },
            { ID::outputLevel, -4.0f } } },

        { "Ambient Clean", {
            { ID::channel, 0 }, { ID::drive, 2.0f }, { ID::inputGain, 0.0f },
            { ID::gateOn, 1 }, { ID::gateThreshold, -66.0f },
            { ID::bass, 0.0f }, { ID::mid, -2.0f }, { ID::treble, 2.0f }, { ID::presence, 3.0f },
            { ID::cabOn, 1 }, { ID::cabModel, 0 },
            { ID::delayOn, 1 }, { ID::delayTime, 620.0f }, { ID::delayFeedback, 0.55f }, { ID::delayMix, 0.35f },
            { ID::reverbOn, 1 }, { ID::reverbSize, 0.9f }, { ID::reverbMix, 0.45f },
            { ID::outputLevel, -4.5f } } },
    };

    return presets;
}
} // namespace riff
