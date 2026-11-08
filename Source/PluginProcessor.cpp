#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using namespace riff;

RiffForgeProcessor::RiffForgeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::mono(),   true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "RiffForge", createParameterLayout())
{
    auto get = [this] (const char* id)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    params.inputGain     = get (ID::inputGain);
    params.gateOn        = get (ID::gateOn);
    params.gateThreshold = get (ID::gateThreshold);
    params.channel       = get (ID::channel);
    params.drive         = get (ID::drive);
    params.bass          = get (ID::bass);
    params.mid           = get (ID::mid);
    params.treble        = get (ID::treble);
    params.presence      = get (ID::presence);
    params.cabOn         = get (ID::cabOn);
    params.cabModel      = get (ID::cabModel);
    params.delayOn       = get (ID::delayOn);
    params.delayTime     = get (ID::delayTime);
    params.delayFeedback = get (ID::delayFeedback);
    params.delayMix      = get (ID::delayMix);
    params.reverbOn      = get (ID::reverbOn);
    params.reverbSize    = get (ID::reverbSize);
    params.reverbMix     = get (ID::reverbMix);
    params.outputLevel   = get (ID::outputLevel);
}

//==============================================================================
bool RiffForgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const auto ok = [] (const juce::AudioChannelSet& s)
    {
        return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo();
    };
    return ok (in) && ok (out);
}

void RiffForgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlockSize = juce::jmax (16, samplesPerBlock);
    mono.setSize (1, maxBlockSize, false, true, false);

    gate.prepare (sampleRate);
    amp.prepare (sampleRate, maxBlockSize);
    tone.prepare (sampleRate);
    cab.prepare (sampleRate, maxBlockSize);
    delay.prepare (sampleRate);

    const auto numOut = (juce::uint32) juce::jlimit (1, 2, getTotalNumOutputChannels());
    reverb.prepare ({ sampleRate, (juce::uint32) maxBlockSize, numOut });
    reverb.reset();

    inputGain.reset (sampleRate, 0.02);
    outputGain.reset (sampleRate, 0.05);

    updateDspParameters();
    inputGain.setCurrentAndTargetValue (inputGain.getTargetValue());
    outputGain.setCurrentAndTargetValue (outputGain.getTargetValue());
    delay.reset(); // start at the current delay time instead of gliding to it

    setLatencySamples (amp.getLatencySamples());
}

void RiffForgeProcessor::updateDspParameters()
{
    inputGain.setTargetValue (dbToGain (params.inputGain->load()));

    gate.setEnabled (params.gateOn->load() > 0.5f);
    gate.setThresholdDecibels (params.gateThreshold->load());

    amp.setChannel ((AmpChannel) juce::jlimit (0, 2, (int) params.channel->load()));
    amp.setDrive (params.drive->load());

    tone.setGains (params.bass->load(), params.mid->load(), params.treble->load(), params.presence->load());

    delay.setParameters (params.delayOn->load() > 0.5f, params.delayTime->load(),
                         params.delayFeedback->load(), params.delayMix->load());

    const bool reverbOn = params.reverbOn->load() > 0.5f;
    const float size = params.reverbSize->load();
    const float mix  = params.reverbMix->load();

    juce::dsp::Reverb::Parameters p;
    p.roomSize   = 0.30f + 0.68f * size;
    p.damping    = 0.55f - 0.25f * size;
    p.width      = 1.0f;
    p.freezeMode = 0.0f;
    p.wetLevel   = reverbOn ? mix * 0.33f : 0.0f;
    p.dryLevel   = reverbOn ? 0.5f * (1.0f - 0.35f * mix) : 0.5f; // 0.5 == unity inside juce::Reverb

    const auto same = [] (float a, float b) { return juce::approximatelyEqual (a, b); };
    if (! (same (p.roomSize, reverbParams.roomSize) && same (p.damping, reverbParams.damping)
           && same (p.wetLevel, reverbParams.wetLevel) && same (p.dryLevel, reverbParams.dryLevel)
           && same (p.width, reverbParams.width) && same (p.freezeMode, reverbParams.freezeMode)))
    {
        reverbParams = p;
        reverb.setParameters (p);
    }

    outputGain.setTargetValue (dbToGain (params.outputLevel->load()));
}

void RiffForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn  = juce::jmin (getTotalNumInputChannels(),  buffer.getNumChannels());
    const int numOut = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());

    if (numSamples == 0 || numOut == 0)
        return;

    updateDspParameters();

    const int cabPath = params.cabOn->load() > 0.5f ? (int) params.cabModel->load() + 1 : 0;
    auto* m = mono.getWritePointer (0);
    float inPeak = 0.0f;

    for (int start = 0; start < numSamples; start += maxBlockSize)
    {
        const int n = juce::jmin (maxBlockSize, numSamples - start);

        // Guitar is mono: average whatever input channels we were given.
        if (numIn == 0)
        {
            juce::FloatVectorOperations::clear (m, n);
        }
        else
        {
            juce::FloatVectorOperations::copy (m, buffer.getReadPointer (0, start), n);
            for (int ch = 1; ch < numIn; ++ch)
                juce::FloatVectorOperations::add (m, buffer.getReadPointer (ch, start), n);
            if (numIn > 1)
                juce::FloatVectorOperations::multiply (m, 1.0f / (float) numIn, n);
        }

        for (int i = 0; i < n; ++i)
        {
            m[i] *= inputGain.getNextValue();
            inPeak = juce::jmax (inPeak, std::abs (m[i]));
        }

        tunerFifo.push (m, n);

        for (int i = 0; i < n; ++i)
            m[i] = gate.processSample (m[i]);

        amp.process (m, n);
        tone.process (m, n);
        cab.process (m, n, cabPath);
        delay.process (m, n);

        for (int ch = 0; ch < numOut; ++ch)
            buffer.copyFrom (ch, start, m, n);
    }

    for (int ch = numOut; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    juce::dsp::AudioBlock<float> block (buffer);
    auto outBlock = block.getSubsetChannelBlock (0, (size_t) numOut);
    reverb.process (juce::dsp::ProcessContextReplacing<float> (outBlock));

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outputGain.getNextValue();
        for (int ch = 0; ch < numOut; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }

    float outPeak = 0.0f;
    for (int ch = 0; ch < numOut; ++ch)
        outPeak = juce::jmax (outPeak, buffer.getMagnitude (ch, 0, numSamples));

    if (inPeak > inputPeak.load())   inputPeak.store (inPeak);
    if (outPeak > outputPeak.load()) outputPeak.store (outPeak);
    gateGain.store (gate.getCurrentGain());
}

void RiffForgeProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numIn  = juce::jmin (getTotalNumInputChannels(),  buffer.getNumChannels());
    const int numOut = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());

    for (int ch = numIn; ch < numOut; ++ch)
    {
        if (numIn > 0) buffer.copyFrom (ch, 0, buffer, 0, 0, buffer.getNumSamples());
        else           buffer.clear (ch, 0, buffer.getNumSamples());
    }
}

//==============================================================================
int RiffForgeProcessor::getNumPrograms()
{
    return (int) factoryPresets().size();
}

const juce::String RiffForgeProcessor::getProgramName (int index)
{
    const auto& presets = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) presets.size()) ? juce::String (presets[(size_t) index].name)
                                                                   : juce::String();
}

void RiffForgeProcessor::setCurrentProgram (int index)
{
    // Some hosts re-send the current program right after restoring a project;
    // re-applying it would wipe the user's tweaks, so only react to real changes.
    if (index != currentProgram)
        loadPreset (index);
}

void RiffForgeProcessor::loadPreset (int index)
{
    const auto& presets = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;

    currentProgram = index;
    const auto& preset = presets[(size_t) index];

    for (auto* p : getParameters())
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (param == nullptr)
            continue;

        float normalised = param->getDefaultValue();
        for (const auto& [id, value] : preset.values)
            if (param->getParameterID() == id)
                normalised = param->convertTo0to1 (value);

        param->beginChangeGesture();
        param->setValueNotifyingHost (normalised);
        param->endChangeGesture();
    }

    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

//==============================================================================
void RiffForgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr)
        return;

    xml->setAttribute ("program", currentProgram);

    {
        const juce::ScopedLock sl (irLock);
        if (irData.getNumSamples() > 0)
        {
            auto* ir = xml->createNewChildElement ("ImpulseResponse");
            ir->setAttribute ("name", irName);
            ir->setAttribute ("sampleRate", irSampleRate);
            juce::MemoryBlock samples (irData.getReadPointer (0), (size_t) irData.getNumSamples() * sizeof (float));
            ir->addTextElement (samples.toBase64Encoding());
        }
    }

    copyXmlToBinary (*xml, destData);
}

void RiffForgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    if (auto* ir = xml->getChildByName ("ImpulseResponse"))
    {
        juce::MemoryBlock samples;
        const auto rate = ir->getDoubleAttribute ("sampleRate", 0.0);
        if (samples.fromBase64Encoding (ir->getAllSubText().trim()) && rate > 0.0)
        {
            const int n = (int) (samples.getSize() / sizeof (float));
            juce::AudioBuffer<float> buffer (1, n);
            std::memcpy (buffer.getWritePointer (0), samples.getData(), (size_t) n * sizeof (float));
            setImpulseResponse (std::move (buffer), rate, ir->getStringAttribute ("name"));
        }
        xml->removeChildElement (ir, true);
    }
    else
    {
        clearImpulseResponse();
    }

    currentProgram = juce::jlimit (0, getNumPrograms() - 1, xml->getIntAttribute ("program", 0));
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::String RiffForgeProcessor::loadImpulseResponse (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return "Cannot read \"" + file.getFileName() + "\". Use WAV, AIFF or FLAC.";

    const auto maxLength = (juce::int64) (reader->sampleRate * 2.0); // cabinet IRs are short
    const int length = (int) juce::jmin (reader->lengthInSamples, maxLength);
    const int channels = (int) reader->numChannels;
    if (length <= 0 || channels <= 0)
        return "\"" + file.getFileName() + "\" is empty.";

    juce::AudioBuffer<float> source (channels, length);
    reader->read (&source, 0, length, 0, true, true);

    juce::AudioBuffer<float> ir (1, length);
    ir.copyFrom (0, 0, source, 0, 0, length);
    for (int ch = 1; ch < channels; ++ch)
        ir.addFrom (0, 0, source, ch, 0, length);
    if (channels > 1)
        ir.applyGain (1.0f / (float) channels);

    return setImpulseResponse (std::move (ir), reader->sampleRate, file.getFileNameWithoutExtension());
}

juce::String RiffForgeProcessor::setImpulseResponse (juce::AudioBuffer<float> ir, double rate, const juce::String& name)
{
    // Trim the silent tail.
    const float peak = ir.getMagnitude (0, 0, ir.getNumSamples());
    if (peak <= 1.0e-6f)
        return "The impulse response is silent.";

    int length = ir.getNumSamples();
    const auto* s = ir.getReadPointer (0);
    while (length > 64 && std::abs (s[length - 1]) < peak * 1.0e-4f)
        --length;
    ir.setSize (1, length, true, false, true);

    // Normalise the energy so different IRs play at similar volumes.
    double energy = 0.0;
    for (int i = 0; i < length; ++i)
        energy += (double) ir.getSample (0, i) * ir.getSample (0, i);
    ir.applyGain ((float) std::sqrt (CabSimulator::irTargetEnergy / energy));

    {
        const juce::ScopedLock sl (irLock);
        irData.makeCopyOf (ir);
        irSampleRate = rate;
        irName = name.isNotEmpty() ? name : juce::String ("Custom IR");
    }

    cab.loadImpulseResponse (std::move (ir), rate);
    return {};
}

void RiffForgeProcessor::clearImpulseResponse()
{
    {
        const juce::ScopedLock sl (irLock);
        irData.setSize (0, 0);
        irName.clear();
    }
    cab.clearImpulseResponse();
}

juce::String RiffForgeProcessor::getImpulseResponseName() const
{
    const juce::ScopedLock sl (irLock);
    return irName;
}

//==============================================================================
juce::AudioProcessorEditor* RiffForgeProcessor::createEditor()
{
    return new RiffForgeEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RiffForgeProcessor();
}
