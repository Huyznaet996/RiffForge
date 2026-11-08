// Offline test for RiffForge.
//
//  * renders a synthetic guitar (Karplus-Strong plucks) through every factory preset into WAV files
//  * checks for NaN/Inf, runaway levels and silence, and that the noise gate closes on hum/hiss
//  * checks loudness balance between presets and between the built-in cab and an IR
//  * checks the tuner on all six open strings
//  * checks the plugin state (incl. embedded IR) survives a save/restore
//  * measures CPU load and saves a screenshot of the editor
//
// Usage: RiffForgeRenderTest [output-folder]

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <random>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  [ OK ] " : "  [FAIL] ") << what << std::endl;
    if (! ok) ++failures;
}

float toDb (double x) { return juce::Decibels::gainToDecibels ((float) x, -150.0f); }

// A few seconds of "guitar": an E power chord strum, single notes and a let-ring tail,
// followed by 2 s of hum + hiss at -75 dBFS to exercise the noise gate.
juce::AudioBuffer<float> makeGuitar (double sr, int& playedSamples)
{
    struct Note { double time, freq; float amp; };
    const Note notes[] = {
        { 0.00,  82.41, 0.30f }, { 0.01, 123.47, 0.25f }, { 0.02, 164.81, 0.22f }, // E5 strum
        { 1.00, 110.00, 0.30f }, { 1.01, 164.81, 0.25f }, { 1.02, 220.00, 0.22f }, // A5
        { 2.00, 196.00, 0.25f }, { 2.25, 220.00, 0.25f }, { 2.50, 246.94, 0.25f },
        { 2.75, 293.66, 0.25f }, { 3.00, 329.63, 0.28f }, { 3.25, 392.00, 0.28f },
        { 3.50,  82.41, 0.35f }, { 3.51, 123.47, 0.30f }, { 3.52, 164.81, 0.28f }, { 3.53, 207.65, 0.25f },
    };

    const double playSeconds = 6.0, noiseSeconds = 2.0;
    playedSamples = (int) (playSeconds * sr);
    juce::AudioBuffer<float> out (1, (int) ((playSeconds + noiseSeconds) * sr));
    out.clear();
    auto* o = out.getWritePointer (0);

    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);

    for (const auto& n : notes)
    {
        const int period = juce::jmax (2, (int) std::round (sr / n.freq));
        std::vector<float> line ((size_t) period);
        float prev = 0.0f;
        for (auto& v : line) { v = 0.5f * (uni (rng) + prev); prev = v; } // slightly dark pick attack

        const int start = (int) (n.time * sr);
        const int end = juce::jmin (playedSamples, start + (int) (2.5 * sr));
        size_t pos = 0;
        for (int i = start; i < end; ++i)
        {
            const size_t next = (pos + 1) % line.size();
            const float s = line[pos];
            line[pos] = 0.4985f * (line[pos] + line[next]);
            pos = next;
            o[i] += s * n.amp;
        }
    }

    // Mains hum + hiss, as from single-coil pickups into an audio interface.
    const float noiseLevel = juce::Decibels::decibelsToGain (-75.0f);
    for (int i = 0; i < out.getNumSamples(); ++i)
    {
        const float hum = std::sin (juce::MathConstants<float>::twoPi * 50.0f * (float) i / (float) sr);
        o[i] += noiseLevel * (0.6f * hum + 0.4f * uni (rng));
    }

    return out;
}

struct RenderStats
{
    bool finite = true;
    float peak = 0.0f;
    double playedRms = 0.0, gatedRms = 0.0;
};

RenderStats render (RiffForgeProcessor& proc, const juce::AudioBuffer<float>& input, int playedSamples,
                    double sr, int blockSize, bool randomBlocks, juce::AudioBuffer<float>* keep = nullptr)
{
    proc.prepareToPlay (sr, blockSize);

    const int total = input.getNumSamples();
    const int numOut = proc.getTotalNumOutputChannels();
    juce::AudioBuffer<float> out (numOut, total);
    juce::AudioBuffer<float> block (juce::jmax (numOut, proc.getTotalNumInputChannels()), blockSize);
    juce::MidiBuffer midi;
    std::mt19937 rng (99);

    for (int pos = 0; pos < total;)
    {
        int n = juce::jmin (blockSize, total - pos);
        if (randomBlocks)
            n = juce::jmin (n, 1 + (int) (rng() % (unsigned) blockSize));

        block.setSize (block.getNumChannels(), n, false, false, true);
        block.clear();
        block.copyFrom (0, 0, input, 0, pos, n);
        proc.processBlock (block, midi);
        for (int ch = 0; ch < numOut; ++ch)
            out.copyFrom (ch, pos, block, ch, 0, n);
        pos += n;
    }

    RenderStats s;
    double sumPlayed = 0.0, sumGated = 0.0;
    const int gatedStart = playedSamples + (int) (0.8 * sr); // allow reverb/delay tails to die
    for (int ch = 0; ch < numOut; ++ch)
    {
        const auto* d = out.getReadPointer (ch);
        for (int i = 0; i < total; ++i)
        {
            if (! std::isfinite (d[i])) s.finite = false;
            s.peak = juce::jmax (s.peak, std::abs (d[i]));
            if (i < playedSamples)    sumPlayed += (double) d[i] * d[i];
            else if (i >= gatedStart) sumGated  += (double) d[i] * d[i];
        }
    }
    s.playedRms = std::sqrt (sumPlayed / ((double) playedSamples * numOut));
    s.gatedRms  = std::sqrt (sumGated / ((double) (total - gatedStart) * numOut));

    if (keep != nullptr)
        keep->makeCopyOf (out);
    return s;
}

void writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sr)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sr)
                                                                                .withNumChannels (buffer.getNumChannels())
                                                                                .withBitsPerSample (24));
    if (writer != nullptr)
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

void setParam (RiffForgeProcessor& proc, const char* id, float value)
{
    auto* p = proc.apvts.getParameter (id);
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}

float getParam (RiffForgeProcessor& proc, const char* id)
{
    return proc.apvts.getRawParameterValue (id)->load();
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                       : juce::File::getCurrentWorkingDirectory().getChildFile ("render-output");
    outDir.createDirectory();
    std::cout << "Output: " << outDir.getFullPathName() << std::endl;

    const double sr = 48000.0;
    int played = 0;
    const auto guitar = makeGuitar (sr, played);
    writeWav (outDir.getChildFile ("00_dry_input.wav"), guitar, sr);

    RiffForgeProcessor proc;
    std::cout << "\nLayout: " << proc.getTotalNumInputChannels() << " in / "
              << proc.getTotalNumOutputChannels() << " out" << std::endl;

    // ---- Every preset ----------------------------------------------------------
    std::cout << "\nPresets (48 kHz, 256-sample blocks):" << std::endl;
    std::vector<double> loudness;
    const auto& presets = riff::factoryPresets();
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        proc.loadPreset (i);
        juce::AudioBuffer<float> out;
        const auto s = render (proc, guitar, played, sr, 256, false, &out);
        writeWav (outDir.getChildFile (juce::String (i + 1).paddedLeft ('0', 2) + "_"
                                       + juce::String (presets[(size_t) i].name).replaceCharacter (' ', '_') + ".wav"), out, sr);

        // Gate test: delay and reverb tails are meant to ring on, so switch them off here.
        setParam (proc, riff::ID::delayOn, 0.0f);
        setParam (proc, riff::ID::reverbOn, 0.0f);
        const auto gated = render (proc, guitar, played, sr, 256, false);
        setParam (proc, riff::ID::gateOn, 0.0f);
        const auto ungated = render (proc, guitar, played, sr, 256, false);

        std::cout << "    " << juce::String (presets[(size_t) i].name).paddedRight (' ', 14)
                  << " rms " << juce::String (toDb (s.playedRms), 1) << " dBFS, peak "
                  << juce::String (toDb (s.peak), 1) << " dBFS | noise: gate off "
                  << juce::String (toDb (ungated.gatedRms), 1) << " dBFS, gate on "
                  << juce::String (toDb (gated.gatedRms), 1) << " dBFS" << std::endl;

        check (s.finite && gated.finite && ungated.finite, "no NaN/Inf");
        check (s.peak < 2.0f, "no runaway level (peak < +6 dBFS)");
        check (toDb (s.playedRms) > -40.0f, "audible output");
        check (toDb (gated.gatedRms) < -85.0f, "noise gate silences hum/hiss between notes");
        loudness.push_back (toDb (s.playedRms));
    }

    const auto [minL, maxL] = std::minmax_element (loudness.begin() + 1, loudness.end());
    check (*maxL - *minL < 9.0, "presets are level-matched within 9 dB (spread "
                                    + juce::String (*maxL - *minL, 1) + " dB)");

    // ---- Robustness: other rates, odd block sizes, extreme settings ------------
    std::cout << "\nRobustness:" << std::endl;
    proc.loadPreset (4); // Modern Metal
    for (double rate : { 44100.0, 96000.0 })
        for (int bs : { 32, 1024 })
        {
            int p2 = 0;
            const auto g2 = makeGuitar (rate, p2);
            const auto s = render (proc, g2, p2, rate, bs, true);
            check (s.finite && s.peak < 2.0f && toDb (s.playedRms) > -40.0f,
                   juce::String (rate / 1000.0, 1) + " kHz, random blocks up to " + juce::String (bs));
        }

    for (auto* id : { riff::ID::drive, riff::ID::bass, riff::ID::mid, riff::ID::treble, riff::ID::presence, riff::ID::inputGain })
        setParam (proc, id, proc.apvts.getParameterRange (id).end);
    setParam (proc, riff::ID::delayOn, 1.0f);
    setParam (proc, riff::ID::delayFeedback, 0.9f);
    setParam (proc, riff::ID::delayMix, 1.0f);
    setParam (proc, riff::ID::reverbMix, 1.0f);
    setParam (proc, riff::ID::reverbSize, 1.0f);
    {
        const auto s = render (proc, guitar, played, sr, 128, false);
        std::cout << "    everything maxed: peak " << juce::String (toDb (s.peak), 1) << " dBFS" << std::endl;
        check (s.finite && s.peak < 16.0f, "all knobs at maximum stays finite and bounded (no runaway)");
    }

    // ---- Impulse response ---------------------------------------------------------
    std::cout << "\nImpulse response loader:" << std::endl;
    const auto irFile = outDir.getChildFile ("test_cab_ir.wav");
    {
        // Use the built-in 4x12 as an IR, so IR and built-in cab should sound (and measure) the same.
        riff::FilterCabinet cabinet;
        cabinet.prepare (sr, riff::CabModel::stack4x12);
        juce::AudioBuffer<float> ir (1, 4096);
        ir.clear();
        ir.setSample (0, 0, 1.0f);
        cabinet.process (ir.getWritePointer (0), ir.getNumSamples());

        double energy = 0.0;
        for (int i = 0; i < ir.getNumSamples(); ++i)
            energy += (double) ir.getSample (0, i) * ir.getSample (0, i);
        std::cout << "    energy of built-in 4x12 response: " << juce::String (energy, 3)
                  << " (CabSimulator::irTargetEnergy = " << riff::CabSimulator::irTargetEnergy << ")" << std::endl;
        writeWav (irFile, ir, sr);
    }

    proc.loadPreset (3); // Classic Rock, built-in 4x12
    const auto builtIn = render (proc, guitar, played, sr, 256, false);

    const auto error = proc.loadImpulseResponse (irFile);
    check (error.isEmpty(), "IR file loads" + (error.isEmpty() ? juce::String() : ": " + error));
    check (proc.hasImpulseResponse() && proc.getImpulseResponseName() == "test_cab_ir", "IR name is reported");
    check (proc.loadImpulseResponse (outDir.getChildFile ("missing.wav")).isNotEmpty(), "missing file gives an error message");

    setParam (proc, riff::ID::cabModel, (float) riff::CabModel::irFile);
    juce::AudioBuffer<float> irOut;
    const auto withIr = render (proc, guitar, played, sr, 256, false, &irOut);
    writeWav (outDir.getChildFile ("20_Classic_Rock_with_IR.wav"), irOut, sr);
    std::cout << "    built-in cab " << juce::String (toDb (builtIn.playedRms), 1) << " dBFS, IR "
              << juce::String (toDb (withIr.playedRms), 1) << " dBFS" << std::endl;
    check (withIr.finite && std::abs (toDb (withIr.playedRms) - toDb (builtIn.playedRms)) < 3.0f,
           "IR level matches built-in cabinet within 3 dB");

    // ---- State save / restore --------------------------------------------------------
    std::cout << "\nState:" << std::endl;
    setParam (proc, riff::ID::drive, 7.3f);
    setParam (proc, riff::ID::delayTime, 512.0f);
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    {
        RiffForgeProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (getParam (restored, riff::ID::drive) - 7.3f) < 0.01f, "drive restored");
        check (std::abs (getParam (restored, riff::ID::delayTime) - 512.0f) < 0.5f, "delay time restored");
        check ((int) getParam (restored, riff::ID::cabModel) == (int) riff::CabModel::irFile, "cab model restored");
        check (restored.hasImpulseResponse() && restored.getImpulseResponseName() == "test_cab_ir", "embedded IR restored");
        check (restored.getCurrentProgram() == proc.getCurrentProgram(), "program restored");

        const auto s = render (restored, guitar, played, sr, 256, false);
        check (std::abs (toDb (s.playedRms) - toDb (withIr.playedRms)) < 0.5f, "restored instance sounds the same");

        juce::MemoryBlock again;
        restored.getStateInformation (again);
        check (again.getSize() == state.getSize(), "state does not grow on repeated save/restore");
    }

    // ---- Tuner ------------------------------------------------------------------------
    std::cout << "\nTuner:" << std::endl;
    for (double f : { 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 61.74 })
    {
        riff::PitchDetector det;
        det.setSampleRate (sr);
        for (int i = 0; i < (int) (0.2 * sr); ++i)
        {
            const double t = i / sr;
            float x = 0.0f;
            for (int h = 1; h <= 8; ++h)
                x += (float) (0.3 / h * std::sin (juce::MathConstants<double>::twoPi * f * h * t + h));
            det.pushSample (x);
        }
        const auto r = det.analyse();
        const double centsOff = 1200.0 * std::log2 (r.frequency / f);
        check (r.valid && std::abs (centsOff) < 2.0,
               juce::String (f, 2) + " Hz -> " + r.noteName + " " + juce::String (r.frequency, 2) + " Hz ("
                   + juce::String (centsOff, 2) + " ct)");
    }
    {
        riff::PitchDetector det;
        det.setSampleRate (sr);
        std::mt19937 rng (5);
        std::uniform_real_distribution<float> uni (-0.001f, 0.001f);
        for (int i = 0; i < 20000; ++i) det.pushSample (uni (rng));
        check (! det.analyse().valid, "quiet noise shows no note");
    }

    // ---- CPU ---------------------------------------------------------------------------
    std::cout << "\nCPU:" << std::endl;
    {
        proc.loadPreset (5); // Lead Solo: two-stage amp, delay, reverb
        setParam (proc, riff::ID::cabModel, (float) riff::CabModel::irFile);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (int k = 0; k < 4; ++k)
            render (proc, guitar, played, sr, 128, false);
        const double elapsed = (juce::Time::getMillisecondCounterHiRes() - start) / 1000.0;
        const double audio = 4.0 * guitar.getNumSamples() / sr;
        std::cout << "    " << juce::String (audio, 1) << " s of audio in " << juce::String (elapsed, 3)
                  << " s -> " << juce::String (100.0 * elapsed / audio, 2) << " % of one core" << std::endl;
        check (elapsed / audio < 0.25, "uses less than 25% of one core");
        std::cout << "    reported latency: " << proc.getLatencySamples() << " samples" << std::endl;
    }

    // ---- Editor screenshot ----------------------------------------------------------------
    std::cout << "\nEditor:" << std::endl;
    {
        proc.loadPreset (4);
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        check (editor != nullptr && editor->getWidth() == RiffForgeEditor::baseWidth, "editor opens at 1000x620");

        // Feed the tuner something so the screenshot shows a note.
        std::vector<float> a ((size_t) sr / 4);
        for (size_t i = 0; i < a.size(); ++i)
            a[i] = 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 110.6 * (double) i / sr);
        proc.tunerFifo.pull ([] (float) {}); // drop what the renders above left behind
        proc.tunerFifo.push (a.data(), (int) a.size());
        proc.inputPeak = 0.35f;
        proc.outputPeak = 0.6f;

        for (int i = 0; i < 4; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);

        const auto snapshot = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto png = outDir.getChildFile ("editor.png");
        png.deleteFile();
        juce::FileOutputStream os (png);
        check (juce::PNGImageFormat().writeImageToStream (snapshot, os), "screenshot saved to " + png.getFileName());

        editor->setSize (RiffForgeEditor::baseWidth * 3 / 2, RiffForgeEditor::baseHeight * 3 / 2);
        check (editor->getWidth() == RiffForgeEditor::baseWidth * 3 / 2, "editor resizes");
    }

    std::cout << "\n" << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " TEST(S) FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
