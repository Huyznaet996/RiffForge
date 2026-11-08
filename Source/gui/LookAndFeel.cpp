#include "LookAndFeel.h"

namespace riff
{
using namespace juce;

RiffLookAndFeel::RiffLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, colours::background);
    setColour (Slider::textBoxTextColourId, colours::text);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId, colours::accentDim);
    setColour (Slider::rotarySliderFillColourId, colours::accent);
    setColour (Slider::rotarySliderOutlineColourId, colours::panelEdge);
    setColour (Label::textColourId, colours::text);
    setColour (TextEditor::textColourId, colours::text);
    setColour (TextEditor::backgroundColourId, colours::background);
    setColour (TextEditor::highlightColourId, colours::accentDim);
    setColour (CaretComponent::caretColourId, colours::accent);
    setColour (ComboBox::backgroundColourId, colours::background);
    setColour (ComboBox::textColourId, colours::text);
    setColour (ComboBox::outlineColourId, colours::panelEdge);
    setColour (ComboBox::arrowColourId, colours::accent);
    setColour (PopupMenu::backgroundColourId, colours::panel);
    setColour (PopupMenu::textColourId, colours::text);
    setColour (PopupMenu::highlightedBackgroundColourId, colours::accent);
    setColour (PopupMenu::highlightedTextColourId, colours::background);
    setColour (TextButton::buttonColourId, colours::background);
    setColour (TextButton::buttonOnColourId, colours::accent);
    setColour (TextButton::textColourOffId, colours::text);
    setColour (TextButton::textColourOnId, colours::background);
    setColour (ToggleButton::textColourId, colours::text);
    setColour (AlertWindow::backgroundColourId, colours::panel);
    setColour (AlertWindow::textColourId, colours::text);
}

void RiffLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height, float sliderPos,
                                        float startAngle, float endAngle, Slider& slider)
{
    const auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float trackWidth = jmax (2.5f, radius * 0.09f);
    const float arcRadius = radius - trackWidth * 0.5f;

    // Track
    Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::panelEdge);
    g.strokePath (track, PathStrokeType (trackWidth, PathStrokeType::curved, PathStrokeType::rounded));

    // Value arc (bipolar parameters like EQ start from the centre)
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0
                         && std::abs (slider.getMinimum() + slider.getMaximum()) < 1.0e-3;
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    if (slider.isEnabled())
    {
        Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, jmin (from, angle), jmax (from, angle), true);
        g.setColour (colours::accent);
        g.strokePath (value, PathStrokeType (trackWidth, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // Knob body
    const float knobRadius = radius * 0.72f;
    const auto knob = Rectangle<float> (knobRadius * 2.0f, knobRadius * 2.0f).withCentre (centre);

    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillEllipse (knob.translated (0.0f, 2.5f).expanded (1.5f));

    g.setGradientFill (ColourGradient (colours::knobTop, knob.getX(), knob.getY(),
                                       colours::knobBottom, knob.getX(), knob.getBottom(), false));
    g.fillEllipse (knob);
    g.setColour (colours::panelEdge.brighter (0.15f));
    g.drawEllipse (knob, 1.2f);

    // Pointer
    Path pointer;
    const float pw = jmax (2.0f, knobRadius * 0.12f);
    pointer.addRoundedRectangle (-pw * 0.5f, -knobRadius + 3.0f, pw, knobRadius * 0.5f, pw * 0.5f);
    g.setColour (slider.isEnabled() ? colours::text : colours::textDim);
    g.fillPath (pointer, AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

void RiffLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& button, bool highlighted, bool)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const float ledSize = jmin (14.0f, bounds.getHeight() - 4.0f);
    const auto led = Rectangle<float> (ledSize, ledSize).withCentre ({ bounds.getX() + ledSize * 0.5f + 2.0f, bounds.getCentreY() });
    const bool on = button.getToggleState();

    if (on)
    {
        g.setColour (colours::accent.withAlpha (0.25f));
        g.fillEllipse (led.expanded (4.0f));
    }

    g.setColour (on ? colours::accent : colours::accentDim.darker (0.6f));
    g.fillEllipse (led);
    g.setColour (on ? colours::accent.brighter (0.6f) : colours::panelEdge);
    g.drawEllipse (led, 1.0f);

    g.setColour (highlighted ? colours::text : colours::text.withAlpha (0.85f));
    g.setFont (FontOptions (13.0f, Font::bold));
    g.drawText (button.getButtonText(), bounds.withTrimmedLeft (ledSize + 9.0f), Justification::centredLeft, true);
}

void RiffLookAndFeel::drawButtonBackground (Graphics& g, Button& button, const Colour&, bool highlighted, bool down)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();

    auto fill = on ? colours::accent : colours::background;
    if (highlighted && ! on) fill = fill.brighter (0.12f);
    if (down) fill = fill.darker (0.15f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (on ? colours::accent.brighter (0.3f) : colours::panelEdge);
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
}

void RiffLookAndFeel::drawComboBox (Graphics& g, int width, int height, bool, int, int, int, int, ComboBox& box)
{
    const auto bounds = Rectangle<int> (width, height).toFloat().reduced (0.5f);
    g.setColour (box.findColour (ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (box.hasKeyboardFocus (true) ? colours::accent : box.findColour (ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, 5.0f, 1.0f);

    Path arrow;
    const float cx = (float) width - 14.0f, cy = (float) height * 0.5f;
    arrow.startNewSubPath (cx - 4.5f, cy - 2.0f);
    arrow.lineTo (cx, cy + 2.5f);
    arrow.lineTo (cx + 4.5f, cy - 2.0f);
    g.setColour (colours::accent);
    g.strokePath (arrow, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
}

Font RiffLookAndFeel::getComboBoxFont (ComboBox&)          { return FontOptions (14.0f); }
Font RiffLookAndFeel::getTextButtonFont (TextButton&, int) { return FontOptions (13.0f, Font::bold); }
Font RiffLookAndFeel::getLabelFont (Label& label)          { return label.getFont(); }

Label* RiffLookAndFeel::createSliderTextBox (Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (FontOptions (12.5f));
    l->setColour (Label::textColourId, colours::textDim);
    l->setColour (Label::backgroundColourId, Colours::transparentBlack);
    l->setColour (Label::outlineColourId, Colours::transparentBlack);
    l->setColour (Label::textWhenEditingColourId, colours::text);
    l->setColour (Label::outlineWhenEditingColourId, colours::accent);
    l->setColour (TextEditor::backgroundColourId, colours::background);
    return l;
}
} // namespace riff
