#include "PluginEditor.h"
#include "Presets.h"
#include "gui/Widgets.h"

using namespace riff;
using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
// All controls live in a fixed 1000x620 panel; the editor scales it to the window size.
class RiffForgeEditor::MainPanel : public juce::Component, private juce::Timer
{
public:
    explicit MainPanel (RiffForgeProcessor& p)
        : proc (p),
          inputGain     (p.apvts, ID::inputGain,     "Gain"),
          gateThreshold (p.apvts, ID::gateThreshold, "Threshold"),
          channel       (p.apvts, ID::channel),
          drive         (p.apvts, ID::drive,         "Drive"),
          bass          (p.apvts, ID::bass,          "Bass"),
          mid           (p.apvts, ID::mid,           "Middle"),
          treble        (p.apvts, ID::treble,        "Treble"),
          presence      (p.apvts, ID::presence,      "Presence"),
          output        (p.apvts, ID::outputLevel,   "Level"),
          delayTime     (p.apvts, ID::delayTime,     "Time"),
          delayFeedback (p.apvts, ID::delayFeedback, "Feedback"),
          delayMix      (p.apvts, ID::delayMix,      "Mix"),
          reverbSize    (p.apvts, ID::reverbSize,    "Size"),
          reverbMix     (p.apvts, ID::reverbMix,     "Mix")
    {
        for (auto* k : { &inputGain, &gateThreshold, &drive, &bass, &mid, &treble, &presence,
                         &output, &delayTime, &delayFeedback, &delayMix, &reverbSize, &reverbMix })
            addAndMakeVisible (*k);

        addAndMakeVisible (channel);
        addAndMakeVisible (tuner);
        addAndMakeVisible (inMeter);
        addAndMakeVisible (outMeter);
        addAndMakeVisible (gateLed);

        auto setupToggle = [this] (juce::ToggleButton& b, const char* paramID, const juce::String& text,
                                   std::unique_ptr<APVTS::ButtonAttachment>& att)
        {
            b.setButtonText (text);
            addAndMakeVisible (b);
            att = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, paramID, b);
        };
        setupToggle (gateOn,   ID::gateOn,   "GATE", gateAtt);
        setupToggle (cabOn,    ID::cabOn,    "ON",   cabOnAtt);
        setupToggle (delayOn,  ID::delayOn,  "ON",   delayOnAtt);
        setupToggle (reverbOn, ID::reverbOn, "ON",   reverbOnAtt);

        // Presets
        const auto& presets = factoryPresets();
        for (int i = 0; i < (int) presets.size(); ++i)
            presetBox.addItem (presets[(size_t) i].name, i + 1);
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
        presetBox.onChange = [this] { proc.loadPreset (presetBox.getSelectedId() - 1); };
        addAndMakeVisible (presetBox);

        // Cabinet
        cabModel.addItemList (cabModelNames(), 1);
        addAndMakeVisible (cabModel);
        cabModelAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, ID::cabModel, cabModel);

        loadIrButton.onClick = [this] { chooseImpulseResponse(); };
        clearIrButton.onClick = [this]
        {
            proc.clearImpulseResponse();
            if ((int) proc.apvts.getRawParameterValue (ID::cabModel)->load() == (int) CabModel::irFile)
                setParameter (ID::cabModel, (float) CabModel::stack4x12);
        };
        addAndMakeVisible (loadIrButton);
        addAndMakeVisible (clearIrButton);

        irLabel.setFont (juce::FontOptions (12.5f));
        irLabel.setColour (juce::Label::textColourId, colours::textDim);
        irLabel.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (irLabel);

        updateIrLabel();
        startTimerHz (30);
    }

    ~MainPanel() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::background);

        // Header
        auto header = juce::Rectangle<float> (16.0f, 12.0f, 260.0f, 64.0f);
        g.setColour (colours::accent);
        g.setFont (juce::FontOptions (34.0f, juce::Font::bold));
        g.drawText ("RIFFFORGE", header.removeFromTop (40.0f), juce::Justification::bottomLeft);
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("GUITAR AMP  &  EFFECTS", header, juce::Justification::topLeft);

        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("PRESET", presetBox.getBounds().translated (0, -17).withHeight (14), juce::Justification::centredLeft);

        for (const auto& s : sections)
            drawSection (g, s.bounds, s.title);
    }

    void resized() override
    {
        sections.clear();

        presetBox.setBounds (290, 36, 230, 30);
        tuner.setBounds (544, 14, 440, 62);

        // ---- Row 1 ---------------------------------------------------------
        const int row1 = 88, h1 = 250, row2 = 350, h2 = 254;
        int x = 16;

        auto inputArea = addSection ("INPUT", x, row1, 236, h1);
        {
            auto c = inputArea.reduced (10, 6);
            inMeter.setBounds (c.removeFromRight (26).withTrimmedBottom (8));
            c.removeFromRight (6);
            auto gateTop = c.removeFromTop (30).withTrimmedLeft (c.getWidth() / 2);
            gateOn.setBounds (gateTop.removeFromLeft (70).withSizeKeepingCentre (70, 24).translated (8, 4));
            gateLed.setBounds (gateTop.removeFromLeft (14).translated (4, 4));
            auto gainArea = c.removeFromLeft (c.getWidth() / 2);
            inputGain.setBounds (gainArea.withSizeKeepingCentre (96, 130).translated (0, -8));
            gateThreshold.setBounds (c.withSizeKeepingCentre (96, 130).translated (0, -8));
        }
        x += 236 + 12;

        auto ampArea = addSection ("AMP", x, row1, 210, h1);
        {
            auto c = ampArea.reduced (12, 8);
            channel.setBounds (c.removeFromTop (28));
            drive.setBounds (c.withSizeKeepingCentre (140, 160).translated (0, 4));
        }
        x += 210 + 12;

        auto eqArea = addSection ("TONE", x, row1, 356, h1);
        {
            auto c = eqArea.reduced (8, 6);
            const int w = c.getWidth() / 4;
            for (auto* k : { &bass, &mid, &treble, &presence })
                k->setBounds (c.removeFromLeft (w).withSizeKeepingCentre (w, 130));
        }
        x += 356 + 12;

        auto outArea = addSection ("OUTPUT", x, row1, 1000 - 16 - x, h1);
        {
            auto c = outArea.reduced (8, 6);
            outMeter.setBounds (c.removeFromRight (26).withTrimmedBottom (8));
            output.setBounds (c.withSizeKeepingCentre (juce::jmin (c.getWidth(), 92), 130));
        }

        // ---- Row 2 ---------------------------------------------------------
        x = 16;
        auto cabArea = addSection ("CABINET", x, row2, 300, h2);
        cabOn.setBounds (x + 300 - 62, row2 + 4, 56, 22);
        {
            auto c = cabArea.reduced (16, 14);
            cabModel.setBounds (c.removeFromTop (30));
            c.removeFromTop (14);
            auto buttons = c.removeFromTop (30);
            loadIrButton.setBounds (buttons.removeFromLeft (buttons.getWidth() * 2 / 3 - 4));
            buttons.removeFromLeft (8);
            clearIrButton.setBounds (buttons);
            c.removeFromTop (12);
            irLabel.setBounds (c);
        }
        x += 300 + 12;

        auto delayArea = addSection ("DELAY", x, row2, 350, h2);
        delayOn.setBounds (x + 350 - 62, row2 + 4, 56, 22);
        {
            auto c = delayArea.reduced (8, 6);
            const int w = c.getWidth() / 3;
            for (auto* k : { &delayTime, &delayFeedback, &delayMix })
                k->setBounds (c.removeFromLeft (w).withSizeKeepingCentre (w, 136));
        }
        x += 350 + 12;

        const int reverbWidth = 1000 - 16 - x;
        auto reverbArea = addSection ("REVERB", x, row2, reverbWidth, h2);
        reverbOn.setBounds (x + reverbWidth - 62, row2 + 4, 56, 22);
        {
            auto c = reverbArea.reduced (8, 6);
            const int w = c.getWidth() / 2;
            for (auto* k : { &reverbSize, &reverbMix })
                k->setBounds (c.removeFromLeft (w).withSizeKeepingCentre (juce::jmin (w, 110), 136));
        }
    }

private:
    struct Section
    {
        juce::String title;
        juce::Rectangle<int> bounds;
    };

    juce::Rectangle<int> addSection (const juce::String& title, int x, int y, int w, int h)
    {
        sections.push_back ({ title, { x, y, w, h } });
        return { x, y + 30, w, h - 30 }; // content area below the title bar
    }

    static void drawSection (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
    {
        const auto r = area.toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

        g.setColour (colours::accent);
        g.fillRoundedRectangle (r.getX() + 12.0f, r.getY() + 10.0f, 3.0f, 12.0f, 1.5f);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        g.drawText (title, (int) r.getX() + 22, (int) r.getY() + 4, 200, 24, juce::Justification::centredLeft);

        g.setColour (colours::panelEdge);
        g.fillRect (r.getX() + 10.0f, r.getY() + 30.0f, r.getWidth() - 20.0f, 1.0f);
    }

    void timerCallback() override
    {
        inMeter.pushLevel (proc.inputPeak.exchange (0.0f));
        outMeter.pushLevel (proc.outputPeak.exchange (0.0f));
        gateLed.setOn (proc.gateGain.load() > 0.5f);

        detector.setSampleRate (proc.currentSampleRate.load());
        proc.tunerFifo.pull ([this] (float s) { detector.pushSample (s); });
        if ((++frameCounter & 1) == 0)
            tuner.setResult (detector.analyse());

        const int program = proc.getCurrentProgram();
        if (presetBox.getSelectedId() != program + 1)
            presetBox.setSelectedId (program + 1, juce::dontSendNotification);

        if ((frameCounter % 15) == 0)
            updateIrLabel();
    }

    void updateIrLabel()
    {
        const auto name = proc.getImpulseResponseName();
        const auto text = proc.hasImpulseResponse()
                              ? "IR: " + name + "\nSelect \"IR File\" above to use it."
                              : juce::String ("No IR file loaded.\nLoad a cabinet impulse response (WAV/AIFF/FLAC) or use a built-in cabinet.");
        if (irLabel.getText() != text)
            irLabel.setText (text, juce::dontSendNotification);
    }

    void chooseImpulseResponse()
    {
        chooser = std::make_unique<juce::FileChooser> ("Load cabinet impulse response", lastIrFolder, "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<MainPanel> (this)] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (safe == nullptr || file == juce::File())
                return;

            lastIrFolder = file.getParentDirectory();
            const auto error = safe->proc.loadImpulseResponse (file);
            if (error.isNotEmpty())
            {
                juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                                  .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                  .withTitle ("RiffForge")
                                                  .withMessage (error)
                                                  .withButton ("OK"),
                                              nullptr);
                return;
            }

            safe->setParameter (ID::cabModel, (float) CabModel::irFile);
            safe->setParameter (ID::cabOn, 1.0f);
            safe->updateIrLabel();
        });
    }

    void setParameter (const char* id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    }

    RiffForgeProcessor& proc;

    Knob inputGain, gateThreshold;
    ChoiceButtons channel;
    Knob drive, bass, mid, treble, presence, output;
    Knob delayTime, delayFeedback, delayMix, reverbSize, reverbMix;

    juce::ToggleButton gateOn, cabOn, delayOn, reverbOn;
    std::unique_ptr<APVTS::ButtonAttachment> gateAtt, cabOnAtt, delayOnAtt, reverbOnAtt;

    juce::ComboBox presetBox, cabModel;
    std::unique_ptr<APVTS::ComboBoxAttachment> cabModelAtt;
    juce::TextButton loadIrButton { "LOAD IR..." }, clearIrButton { "CLEAR" };
    juce::Label irLabel;

    TunerDisplay tuner;
    LevelMeter inMeter { "IN" }, outMeter { "OUT" };
    ActivityLed gateLed;
    PitchDetector detector;
    int frameCounter = 0;

    std::unique_ptr<juce::FileChooser> chooser;
    inline static juce::File lastIrFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    std::vector<Section> sections;
};

//==============================================================================
RiffForgeEditor::RiffForgeEditor (RiffForgeProcessor& p)
    : AudioProcessorEditor (p)
{
    setLookAndFeel (&lookAndFeel);
    panel = std::make_unique<MainPanel> (p);
    addAndMakeVisible (*panel);
    panel->sendLookAndFeelChange(); // children were built before they could see our look-and-feel

    setResizable (true, true);
    setResizeLimits (baseWidth * 7 / 10, baseHeight * 7 / 10, baseWidth * 2, baseHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);

    setSize (baseWidth, baseHeight);
}

RiffForgeEditor::~RiffForgeEditor()
{
    panel = nullptr;
    setLookAndFeel (nullptr);
}

void RiffForgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void RiffForgeEditor::resized()
{
    panel->setBounds (0, 0, baseWidth, baseHeight);
    panel->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) baseWidth));
}
