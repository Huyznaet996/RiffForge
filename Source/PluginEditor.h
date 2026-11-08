#pragma once

#include "PluginProcessor.h"
#include "gui/LookAndFeel.h"

class RiffForgeEditor : public juce::AudioProcessorEditor
{
public:
    static constexpr int baseWidth = 1000;
    static constexpr int baseHeight = 620;

    explicit RiffForgeEditor (RiffForgeProcessor&);
    ~RiffForgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class MainPanel;

    riff::RiffLookAndFeel lookAndFeel; // must outlive the panel
    std::unique_ptr<MainPanel> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiffForgeEditor)
};
