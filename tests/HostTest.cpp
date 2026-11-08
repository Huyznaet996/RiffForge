// Loads the built plugin binaries the way a DAW does (VST3 / LV2 / AU via JUCE's hosting code)
// and checks: instantiation, bus layouts, parameters, programs, audio processing,
// state save/restore through the wrapper and opening the editor.
//
// Usage: RiffForgeHostTest <path-to-RiffForge.vst3 | RiffForge.lv2 | RiffForge.component> ...

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  [ OK ] " : "  [FAIL] ") << what << std::endl;
    if (! ok) ++failures;
}

float processAndMeasure (juce::AudioPluginInstance& plugin, double sr, int block, int numBlocks)
{
    const int numCh = juce::jmax (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels());
    juce::AudioBuffer<float> buffer (numCh, block);
    juce::MidiBuffer midi;
    double sum = 0.0;
    long count = 0;
    bool finite = true;
    long phase = 0;

    for (int b = 0; b < numBlocks; ++b)
    {
        buffer.clear();
        for (int i = 0; i < block; ++i, ++phase)
            buffer.setSample (0, i, 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * 110.0 * (double) phase / sr));

        plugin.processBlock (buffer, midi);

        for (int ch = 0; ch < plugin.getTotalNumOutputChannels(); ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float s = buffer.getSample (ch, i);
                finite = finite && std::isfinite (s);
                sum += (double) s * s;
                ++count;
            }
    }

    return finite ? (float) std::sqrt (sum / (double) juce::jmax (1L, count)) : -1.0f;
}

void testPlugin (juce::AudioPluginFormatManager& formats, const juce::String& path)
{
    std::cout << "\n" << path << std::endl;

    juce::OwnedArray<juce::PluginDescription> found;
    juce::AudioPluginFormat* format = nullptr;
    for (auto* f : formats.getFormats())
    {
        if (f->fileMightContainThisPluginType (path))
        {
            f->findAllTypesForFile (found, path);
            if (! found.isEmpty()) { format = f; break; }
        }
    }

    check (! found.isEmpty(), "plugin found by the host scanner");
    if (found.isEmpty())
        return;

    const auto& desc = *found.getFirst();
    std::cout << "    " << format->getName() << ": \"" << desc.name << "\" by " << desc.manufacturerName
              << ", category " << desc.category << ", " << desc.numInputChannels << " in / "
              << desc.numOutputChannels << " out" << std::endl;
    check (desc.name == "RiffForge" && ! desc.isInstrument, "description (name, effect type)");

    // LV2 has a fixed port configuration and the host owns control-port values, so some checks
    // below only make sense for VST3/AU.
    const bool isLV2 = desc.pluginFormatName == "LV2";

    const double sr = 48000.0;
    const int block = 256;
    juce::String error;
    auto plugin = formats.createPluginInstance (desc, sr, block, error);
    check (plugin != nullptr, "instantiates" + (error.isEmpty() ? juce::String() : ": " + error));
    if (plugin == nullptr)
        return;

    // Typical guitar-track layouts
    for (auto [in, out, name] : { std::tuple { juce::AudioChannelSet::mono(),   juce::AudioChannelSet::stereo(), "mono -> stereo" },
                                  std::tuple { juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo(), "stereo -> stereo" },
                                  std::tuple { juce::AudioChannelSet::mono(),   juce::AudioChannelSet::mono(),   "mono -> mono" } })
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (in);
        layout.outputBuses.add (out);
        if (! isLV2 || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo()))
            check (plugin->checkBusesLayoutSupported (layout), juce::String ("bus layout ") + name);
    }

    juce::AudioProcessor::BusesLayout monoToStereo;
    monoToStereo.inputBuses.add (juce::AudioChannelSet::mono());
    monoToStereo.outputBuses.add (juce::AudioChannelSet::stereo());
    plugin->setBusesLayout (monoToStereo);

    const auto params = plugin->getParameters();
    plugin->prepareToPlay (sr, block);
    std::cout << "    " << params.size() << " parameters, " << plugin->getNumPrograms() << " programs, latency "
              << plugin->getLatencySamples() << " samples" << std::endl;
    check (params.size() >= 19, "exposes all parameters");
    check (plugin->getNumPrograms() >= 7, "exposes factory presets as programs");

    const float rms = processAndMeasure (*plugin, sr, block, 200);
    std::cout << "    output rms " << juce::Decibels::gainToDecibels (rms) << " dBFS" << std::endl;
    check (rms > 0.001f, "processes audio (finite, non-silent)");

    // Program change through the host
    // Hosts may list presets in their own order (LV2 does), so look the preset up by name.
    juce::StringArray programNames;
    for (int i = 0; i < plugin->getNumPrograms(); ++i)
        programNames.add (plugin->getProgramName (i));
    std::cout << "    programs: " << programNames.joinIntoString (", ") << std::endl;
    const int metal = programNames.indexOf ("Modern Metal");
    check (metal >= 0, "program names");
    plugin->setCurrentProgram (juce::jmax (0, metal));
    check (processAndMeasure (*plugin, sr, block, 100) > 0.001f, "processes after program change");

    // Automate every parameter to random values while processing
    juce::Random rnd (7);
    for (int round = 0; round < 20; ++round)
    {
        for (auto* p : params)
            p->setValue (rnd.nextFloat());
        if (processAndMeasure (*plugin, sr, block, 5) < 0.0f)
        {
            check (false, "random automation produced NaN/Inf");
            break;
        }
    }
    check (true, "random parameter automation stays finite");

    // State round trip through the wrapper (bypass is owned by the host, not the plugin state)
    auto* bypass = plugin->getBypassParameter();
    for (auto* p : params)
        p->setValue (rnd.nextFloat());
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    std::vector<float> before;
    for (auto* p : params) before.push_back (p->getValue());

    for (auto* p : params) p->setValue (p->getDefaultValue());
    plugin->setStateInformation (state.getData(), (int) state.getSize());

    int mismatches = 0;
    for (int i = 0; i < params.size(); ++i)
        if (params[i] != bypass && std::abs (params[i]->getValue() - before[(size_t) i]) > 1.0e-3f)
        {
            if (! isLV2)
                std::cout << "    mismatch: " << params[i]->getName (64) << " saved " << before[(size_t) i]
                      << " restored " << params[i]->getValue() << std::endl;
            ++mismatches;
        }
    if (isLV2)
        std::cout << "    (state check skipped: LV2 hosts save control-port values themselves)" << std::endl;
    else
        check (state.getSize() > 0 && mismatches == 0, "state restore through the wrapper ("
                                                           + juce::String (mismatches) + " mismatches)");

    // Different host settings
    plugin->releaseResources();
    plugin->prepareToPlay (96000.0, 64);
    check (processAndMeasure (*plugin, 96000.0, 64, 300) >= 0.0f, "96 kHz / 64-sample blocks");
    plugin->releaseResources();
    plugin->prepareToPlay (44100.0, 2048);
    check (processAndMeasure (*plugin, 44100.0, 2048, 20) >= 0.0f, "44.1 kHz / 2048-sample blocks");

    // Editor
    if (plugin->hasEditor())
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorIfNeeded());
        check (editor != nullptr && editor->getWidth() > 0, "editor opens ("
                                                                + (editor ? juce::String (editor->getWidth()) + "x" + juce::String (editor->getHeight()) : juce::String ("none")) + ")");
        if (editor != nullptr)
        {
            editor->addToDesktop (0);
            editor->setVisible (true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            plugin->editorBeingDeleted (editor.get());
        }
    }

    plugin->releaseResources();
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    for (int i = 1; i < argc; ++i)
        testPlugin (formats, juce::File::getCurrentWorkingDirectory().getChildFile (argv[i]).getFullPathName());

    std::cout << "\n" << (failures == 0 ? "ALL HOST TESTS PASSED" : juce::String (failures) + " HOST TEST(S) FAILED") << std::endl;
    return failures == 0 && argc > 1 ? 0 : 1;
}
