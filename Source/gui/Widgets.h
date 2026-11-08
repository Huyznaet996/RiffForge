#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeel.h"
#include "../dsp/PitchDetector.h"

namespace riff
{
// Rotary knob with a caption above and the parameter value below.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& caption)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (220);
        slider.setPopupDisplayEnabled (false, false, nullptr);
        addAndMakeVisible (slider);

        label.setText (caption.toUpperCase(), juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, colours::text);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramID, slider);
        // Double-click returns to the parameter's default value.
        if (auto* p = state.getParameter (paramID))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (18));
        slider.setBounds (r);
    }

    juce::Slider slider;

private:
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A row of radio buttons bound to a choice parameter (amp channel selector).
class ChoiceButtons : public juce::Component
{
public:
    ChoiceButtons (juce::AudioProcessorValueTreeState& state, const juce::String& paramID)
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramID));
        jassert (param != nullptr);

        for (int i = 0; i < param->choices.size(); ++i)
        {
            auto* b = buttons.add (new juce::TextButton (param->choices[i].toUpperCase()));
            b->setClickingTogglesState (false);
            b->onClick = [this, i] { attachment->setValueAsCompleteGesture ((float) i); };
            addAndMakeVisible (b);
        }

        attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
        {
            for (int i = 0; i < buttons.size(); ++i)
                buttons[i]->setToggleState (i == juce::roundToInt (v), juce::dontSendNotification);
        });
        attachment->sendInitialUpdate();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int w = r.getWidth() / juce::jmax (1, buttons.size());
        for (auto* b : buttons)
            b->setBounds (r.removeFromLeft (w).reduced (2, 0));
    }

private:
    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// Vertical peak meter with a falling peak hold.
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (juce::String name) : caption (std::move (name)) {}

    void pushLevel (float linearPeak)
    {
        const float db = juce::Decibels::gainToDecibels (linearPeak, -60.0f);
        level = db > level ? db : juce::jmax (-60.0f, level - 1.5f);
        if (db >= hold) { hold = db; holdFrames = 30; }
        else if (--holdFrames < 0) hold = juce::jmax (-60.0f, hold - 1.0f);
        clipped = clipped || linearPeak >= 1.0f;
        repaint();
    }

    void mouseDown (const juce::MouseEvent&) override { clipped = false; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (caption, r.removeFromBottom (14.0f), juce::Justification::centred);

        auto bar = r.reduced (2.0f, 0.0f);
        g.setColour (colours::background);
        g.fillRoundedRectangle (bar, 3.0f);

        const auto toY = [&] (float db) { return juce::jmap (db, -60.0f, 0.0f, bar.getBottom(), bar.getY()); };

        juce::ColourGradient grad (colours::bad, 0.0f, toY (0.0f), colours::good, 0.0f, toY (-60.0f), false);
        grad.addColour (0.12, colours::warn);
        grad.addColour (0.3, colours::good);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bar.withTop (toY (level)), 3.0f);

        g.setColour (colours::text);
        g.fillRect (bar.getX(), toY (hold) - 1.0f, bar.getWidth(), 2.0f);

        g.setColour (clipped ? colours::bad : colours::panelEdge);
        g.fillRoundedRectangle (bar.withHeight (4.0f), 2.0f);
    }

private:
    juce::String caption;
    float level = -60.0f, hold = -60.0f;
    int holdFrames = 0;
    bool clipped = false;
};

// Chromatic tuner readout: note, octave, frequency and a cents needle.
class TunerDisplay : public juce::Component
{
public:
    void setResult (const PitchDetector::Result& r)
    {
        if (r.valid)
        {
            // Smooth the needle a little, but jump immediately on a new note.
            cents = (r.midiNote == note && hasNote) ? cents + 0.35f * (r.cents - cents) : r.cents;
            note = r.midiNote;
            name = r.noteName;
            frequency = r.frequency;
            hasNote = true;
            staleFrames = 0;
        }
        else if (++staleFrames > 12)
        {
            hasNote = false;
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::background);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        auto inner = r.reduced (10.0f, 6.0f);
        auto noteArea = inner.removeFromLeft (62.0f);
        const bool inTune = hasNote && std::abs (cents) < 5.0f;

        g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
        g.setColour (hasNote ? (inTune ? colours::good : colours::text) : colours::textDim);
        g.drawText (hasNote ? name : juce::String ("--"), noteArea, juce::Justification::centred);

        auto info = inner.removeFromTop (16.0f);
        g.setFont (juce::FontOptions (11.5f));
        g.setColour (colours::textDim);
        g.drawText ("TUNER", info, juce::Justification::centredLeft);
        if (hasNote)
            g.drawText (juce::String (frequency, 1) + " Hz   " + (cents >= 0 ? "+" : "") + juce::String (juce::roundToInt (cents)) + " ct",
                        info, juce::Justification::centredRight);

        // Scale -50..+50 cents
        auto scale = inner.reduced (2.0f, 4.0f);
        const float mid = scale.getCentreX();
        g.setColour (colours::panelEdge);
        for (int c = -50; c <= 50; c += 10)
        {
            const float x = juce::jmap ((float) c, -50.0f, 50.0f, scale.getX(), scale.getRight());
            const float h = (c == 0) ? scale.getHeight() : scale.getHeight() * 0.45f;
            g.fillRect (x - 0.5f, scale.getCentreY() - h * 0.5f, 1.0f, h);
        }

        g.setColour (colours::good.withAlpha (0.18f));
        const float zone = scale.getWidth() * 0.05f;
        g.fillRoundedRectangle (mid - zone, scale.getY(), zone * 2.0f, scale.getHeight(), 3.0f);

        if (hasNote)
        {
            const float x = juce::jmap (juce::jlimit (-50.0f, 50.0f, cents), -50.0f, 50.0f, scale.getX(), scale.getRight());
            g.setColour (inTune ? colours::good : colours::accent);
            g.fillRoundedRectangle (x - 2.0f, scale.getY() - 2.0f, 4.0f, scale.getHeight() + 4.0f, 2.0f);
        }
    }

private:
    bool hasNote = false;
    int note = 0, staleFrames = 0;
    float cents = 0.0f, frequency = 0.0f;
    juce::String name;
};

// Small LED that lights while the noise gate lets the signal through.
class ActivityLed : public juce::Component
{
public:
    void setOn (bool shouldBeOn)
    {
        if (shouldBeOn != on) { on = shouldBeOn; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().withSizeKeepingCentre (8.0f, 8.0f);
        g.setColour (on ? colours::good : colours::good.withAlpha (0.15f));
        g.fillEllipse (r);
    }

private:
    bool on = false;
};
} // namespace riff
