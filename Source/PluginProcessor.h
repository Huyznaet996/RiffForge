#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include "Parameters.h"
#include "dsp/NoiseGate.h"
#include "dsp/AmpStage.h"
#include "dsp/ToneStack.h"
#include "dsp/CabSimulator.h"
#include "dsp/Delay.h"
#include "dsp/PitchDetector.h"

class RiffForgeProcessor : public juce::AudioProcessor
{
public:
    static constexpr const char* pluginName = "RiffForge";

    RiffForgeProcessor();
    ~RiffForgeProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return pluginName; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}
    void loadPreset (int index); // always applies the preset (used by the editor)

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Impulse responses (message thread). Returns an error message, empty on success.
    juce::String loadImpulseResponse (const juce::File& file);
    void clearImpulseResponse();
    juce::String getImpulseResponseName() const;
    bool hasImpulseResponse() const { return cab.hasImpulseResponse(); }

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    // Read by the editor
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f }, gateGain { 0.0f };
    std::atomic<double> currentSampleRate { 44100.0 };
    riff::SampleFifo tunerFifo;

private:
    juce::String setImpulseResponse (juce::AudioBuffer<float> ir, double irSampleRate, const juce::String& name);

    struct Params
    {
        std::atomic<float>* inputGain;
        std::atomic<float>* gateOn;
        std::atomic<float>* gateThreshold;
        std::atomic<float>* channel;
        std::atomic<float>* drive;
        std::atomic<float>* bass;
        std::atomic<float>* mid;
        std::atomic<float>* treble;
        std::atomic<float>* presence;
        std::atomic<float>* cabOn;
        std::atomic<float>* cabModel;
        std::atomic<float>* delayOn;
        std::atomic<float>* delayTime;
        std::atomic<float>* delayFeedback;
        std::atomic<float>* delayMix;
        std::atomic<float>* reverbOn;
        std::atomic<float>* reverbSize;
        std::atomic<float>* reverbMix;
        std::atomic<float>* outputLevel;
    };

    void updateDspParameters();

    Params params {};
    riff::NoiseGate gate;
    riff::AmpStage amp;
    riff::ToneStack tone;
    riff::CabSimulator cab;
    riff::Delay delay;
    juce::dsp::Reverb reverb;
    juce::dsp::Reverb::Parameters reverbParams;
    juce::SmoothedValue<float> inputGain { 1.0f }, outputGain { 1.0f };
    juce::AudioBuffer<float> mono;
    int maxBlockSize = 512;
    int currentProgram = 0;

    // Impulse response kept in the plugin state so projects stay portable.
    mutable juce::CriticalSection irLock;
    juce::AudioBuffer<float> irData;
    double irSampleRate = 0.0;
    juce::String irName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiffForgeProcessor)
};
