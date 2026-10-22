#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class AntiTuneLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AntiTuneLookAndFeel()
    {
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff141824));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff2c3550));
        setColour(juce::ComboBox::textColourId, juce::Colours::white);
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff141824));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff00d4ff).withAlpha(0.25f));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, const float rotaryStartAngle,
                          const float rotaryEndAngle, juce::Slider& slider) override
    {
        const float radius = static_cast<float>(juce::jmin(width / 2, height / 2)) - 8.0f;
        const float centreX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
        const float centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        const float rx = centreX - radius;
        const float ry = centreY - radius;
        const float rw = radius * 2.0f;
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        const bool isVocoderKnob = slider.getName().containsIgnoreCase("Vocoder");
        const bool isPitchKnob   = slider.getName().containsIgnoreCase("Pitch");

        const juce::Colour accent = isVocoderKnob
            ? juce::Colour(0xfff43f5e)
            : (isPitchKnob ? juce::Colour(0xffa855f7) : juce::Colour(0xff00f0ff));

        g.setColour(juce::Colour(0xff111522));
        g.fillEllipse(rx, ry, rw, rw);

        g.setColour(juce::Colour(0xff242c44));
        g.drawEllipse(rx, ry, rw, rw, 1.5f);

        juce::Path bgArc;
        bgArc.addCentredArc(centreX, centreY, radius - 3.0f, radius - 3.0f,
                            0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(0xff22293e));
        g.strokePath(bgArc, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path valArc;
        valArc.addCentredArc(centreX, centreY, radius - 3.0f, radius - 3.0f,
                             0.0f, rotaryStartAngle, angle, true);
        g.setColour(slider.isEnabled() ? accent : accent.withAlpha(0.3f));
        g.strokePath(valArc, juce::PathStrokeType(4.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path pointer;
        const float pointerLen = radius * 0.62f;
        pointer.addRoundedRectangle(-1.8f, -radius + 5.0f, 3.6f, pointerLen, 1.5f);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
        g.setColour(slider.isEnabled() ? juce::Colours::white : juce::Colours::grey);
        g.fillPath(pointer);
    }
};

class AntiTuneAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit AntiTuneAudioProcessorEditor(AntiTuneAudioProcessor&);
    ~AntiTuneAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    void timerCallback() override;
    void drawLaserGauge(juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawPitchHistoryGraph(juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawScaleKeyboard(juce::Graphics& g, juce::Rectangle<float> bounds);

    AntiTuneAudioProcessor& audioProcessor;
    AntiTuneLookAndFeel customLookAndFeel;

    juce::ComboBox keyCombo;
    juce::ComboBox scaleCombo;
    juce::ToggleButton laserModeButton { "T-PAIN 0ms SNAP" };
    juce::ToggleButton vocoderOnButton { "VOCODER ON" };
    juce::ToggleButton testVoiceButton { "TEST VOICE" };

    juce::Slider retuneSlider;
    juce::Slider correctionSlider;
    juce::Slider pitchShiftSlider;
    juce::Slider vocoderMixSlider;
    juce::Slider vocoderToneSlider;
    juce::Slider gainSlider;

    juce::Label retuneLabel;
    juce::Label correctionLabel;
    juce::Label pitchShiftLabel;
    juce::Label vocoderMixLabel;
    juce::Label vocoderToneLabel;
    juce::Label gainLabel;

    using APVTS = juce::AudioProcessorValueTreeState;
    std::unique_ptr<APVTS::ComboBoxAttachment> keyAttach;
    std::unique_ptr<APVTS::ComboBoxAttachment> scaleAttach;
    std::unique_ptr<APVTS::ButtonAttachment>   laserModeAttach;
    std::unique_ptr<APVTS::ButtonAttachment>   vocoderOnAttach;
    std::unique_ptr<APVTS::SliderAttachment>   retuneAttach;
    std::unique_ptr<APVTS::SliderAttachment>   correctionAttach;
    std::unique_ptr<APVTS::SliderAttachment>   pitchShiftAttach;
    std::unique_ptr<APVTS::SliderAttachment>   vocoderMixAttach;
    std::unique_ptr<APVTS::SliderAttachment>   vocoderToneAttach;
    std::unique_ptr<APVTS::SliderAttachment>   gainAttach;

    juce::Rectangle<float> keyboardBounds;
    std::array<juce::Rectangle<float>, 12> noteKeyRects;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AntiTuneAudioProcessorEditor)
};
