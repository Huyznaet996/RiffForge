#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace riff
{
// Parameter IDs. Never rename these: they are stored in DAW projects and presets.
namespace ID
{
    inline constexpr const char* inputGain     = "inputGain";
    inline constexpr const char* gateOn        = "gateOn";
    inline constexpr const char* gateThreshold = "gateThreshold";
    inline constexpr const char* channel       = "channel";
    inline constexpr const char* drive         = "drive";
    inline constexpr const char* bass          = "bass";
    inline constexpr const char* mid           = "mid";
    inline constexpr const char* treble        = "treble";
    inline constexpr const char* presence      = "presence";
    inline constexpr const char* cabOn         = "cabOn";
    inline constexpr const char* cabModel      = "cabModel";
    inline constexpr const char* delayOn       = "delayOn";
    inline constexpr const char* delayTime     = "delayTime";
    inline constexpr const char* delayFeedback = "delayFeedback";
    inline constexpr const char* delayMix      = "delayMix";
    inline constexpr const char* reverbOn      = "reverbOn";
    inline constexpr const char* reverbSize    = "reverbSize";
    inline constexpr const char* reverbMix     = "reverbMix";
    inline constexpr const char* outputLevel   = "outputLevel";
}

enum class AmpChannel { clean = 0, crunch, lead };
enum class CabModel   { combo1x12 = 0, stack4x12, irFile };

inline juce::StringArray channelNames() { return { "Clean", "Crunch", "Lead" }; }
inline juce::StringArray cabModelNames() { return { "1x12 Combo", "4x12 Stack", "IR File" }; }

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using APF = AudioParameterFloat;
    using APB = AudioParameterBool;
    using APC = AudioParameterChoice;

    auto dB = [] (float v, int) { return String (v, 1) + " dB"; };
    auto ms = [] (float v, int) { return String (roundToInt (v)) + " ms"; };
    auto percent = [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; };
    auto knob10 = [] (float v, int) { return String (v, 1); };

    auto dBAttr      = AudioParameterFloatAttributes().withStringFromValueFunction (dB).withLabel ("dB");
    auto percentAttr = AudioParameterFloatAttributes().withStringFromValueFunction (percent)
                                                      .withValueFromStringFunction ([] (const String& s) { return s.getFloatValue() / 100.0f; });

    AudioProcessorValueTreeState::ParameterLayout layout;
    const int v = 1; // parameter version hint (AU/VST3)

    // Input section
    layout.add (std::make_unique<APF> (ParameterID { ID::inputGain, v }, "Input Gain",
                                       NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, dBAttr));
    layout.add (std::make_unique<APB> (ParameterID { ID::gateOn, v }, "Gate On", true));
    layout.add (std::make_unique<APF> (ParameterID { ID::gateThreshold, v }, "Gate Threshold",
                                       NormalisableRange<float> (-90.0f, -20.0f, 0.1f), -64.0f, dBAttr));

    // Amp
    layout.add (std::make_unique<APC> (ParameterID { ID::channel, v }, "Channel", channelNames(), 1));
    layout.add (std::make_unique<APF> (ParameterID { ID::drive, v }, "Drive",
                                       NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f,
                                       AudioParameterFloatAttributes().withStringFromValueFunction (knob10)));

    // Tone stack
    auto eq = NormalisableRange<float> (-12.0f, 12.0f, 0.1f);
    layout.add (std::make_unique<APF> (ParameterID { ID::bass, v },     "Bass",     eq, 0.0f, dBAttr));
    layout.add (std::make_unique<APF> (ParameterID { ID::mid, v },      "Middle",   eq, 0.0f, dBAttr));
    layout.add (std::make_unique<APF> (ParameterID { ID::treble, v },   "Treble",   eq, 0.0f, dBAttr));
    layout.add (std::make_unique<APF> (ParameterID { ID::presence, v }, "Presence", eq, 0.0f, dBAttr));

    // Cabinet
    layout.add (std::make_unique<APB> (ParameterID { ID::cabOn, v }, "Cabinet On", true));
    layout.add (std::make_unique<APC> (ParameterID { ID::cabModel, v }, "Cabinet", cabModelNames(), 1));

    // Delay
    auto timeRange = NormalisableRange<float> (20.0f, 1500.0f, 1.0f);
    timeRange.setSkewForCentre (400.0f);
    layout.add (std::make_unique<APB> (ParameterID { ID::delayOn, v }, "Delay On", false));
    layout.add (std::make_unique<APF> (ParameterID { ID::delayTime, v }, "Delay Time", timeRange, 380.0f,
                                       AudioParameterFloatAttributes().withStringFromValueFunction (ms).withLabel ("ms")));
    layout.add (std::make_unique<APF> (ParameterID { ID::delayFeedback, v }, "Delay Feedback",
                                       NormalisableRange<float> (0.0f, 0.9f, 0.01f), 0.3f, percentAttr));
    layout.add (std::make_unique<APF> (ParameterID { ID::delayMix, v }, "Delay Mix",
                                       NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.25f, percentAttr));

    // Reverb
    layout.add (std::make_unique<APB> (ParameterID { ID::reverbOn, v }, "Reverb On", true));
    layout.add (std::make_unique<APF> (ParameterID { ID::reverbSize, v }, "Reverb Size",
                                       NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.45f, percentAttr));
    layout.add (std::make_unique<APF> (ParameterID { ID::reverbMix, v }, "Reverb Mix",
                                       NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.15f, percentAttr));

    // Output
    layout.add (std::make_unique<APF> (ParameterID { ID::outputLevel, v }, "Output Level",
                                       NormalisableRange<float> (-36.0f, 12.0f, 0.1f), 0.0f, dBAttr));

    return layout;
}
} // namespace riff
