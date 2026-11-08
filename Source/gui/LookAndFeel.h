#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace riff::colours
{
    const juce::Colour background { 0xff15161a };
    const juce::Colour panel      { 0xff22242b };
    const juce::Colour panelEdge  { 0xff31343d };
    const juce::Colour accent     { 0xffff9f2e };
    const juce::Colour accentDim  { 0xff6b4720 };
    const juce::Colour text       { 0xffe9e6e1 };
    const juce::Colour textDim    { 0xff8b8f99 };
    const juce::Colour knobTop    { 0xff3a3d46 };
    const juce::Colour knobBottom { 0xff1c1d22 };
    const juce::Colour good       { 0xff52d273 };
    const juce::Colour warn       { 0xffffc93c };
    const juce::Colour bad        { 0xffff4d4d };
}

namespace riff
{
class RiffLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RiffLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};
} // namespace riff
