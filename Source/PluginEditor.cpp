#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
void setupRotaryKnob(juce::Slider& slider, juce::Label& label, const juce::String& name, const juce::String& suffix)
{
    slider.setName(name);
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 20);
    slider.setTextValueSuffix(suffix);
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe2e8f0));
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    label.setText(name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::Font(juce::FontOptions(11.5f).withStyle("Bold")));
    label.setColour(juce::Label::textColourId, juce::Colour(0xff94a3b8));
}
} // namespace

AntiTuneAudioProcessorEditor::AntiTuneAudioProcessorEditor(AntiTuneAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&customLookAndFeel);
    setSize(860, 565);

    auto& apvts = audioProcessor.getAPVTS();

    keyCombo.addItemList({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    addAndMakeVisible(keyCombo);
    keyAttach = std::make_unique<APVTS::ComboBoxAttachment>(apvts, "rootKey", keyCombo);
    keyCombo.onChange = [this] { audioProcessor.clearNoteOverrides(); repaint(); };

    scaleCombo.addItemList({
        "Natural Minor",
        "Major",
        "Pentatonic Minor (T-Pain / Trap)",
        "Harmonic Minor",
        "Trap Phrygian (Dark)",
        "Pentatonic Major",
        "Dorian",
        "Mixolydian",
        "Blues",
        "Chromatic (All 12 Notes)"
    }, 1);
    addAndMakeVisible(scaleCombo);
    scaleAttach = std::make_unique<APVTS::ComboBoxAttachment>(apvts, "scaleType", scaleCombo);
    scaleCombo.onChange = [this] { audioProcessor.clearNoteOverrides(); repaint(); };

    // T-Pain 0ms Mode toggle
    laserModeButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xff00f0ff));
    laserModeButton.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff00f0ff));
    addAndMakeVisible(laserModeButton);
    laserModeAttach = std::make_unique<APVTS::ButtonAttachment>(apvts, "laserMode", laserModeButton);

    // Separate Vocoder On/Off toggle
    vocoderOnButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfff43f5e));
    vocoderOnButton.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xfff43f5e));
    addAndMakeVisible(vocoderOnButton);
    vocoderOnAttach = std::make_unique<APVTS::ButtonAttachment>(apvts, "vocoderOn", vocoderOnButton);

    // Test Voice toggle
    testVoiceButton.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffa855f7));
    testVoiceButton.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xffa855f7));
    testVoiceButton.setToggleState(audioProcessor.isTestSynthActive(), juce::dontSendNotification);
    testVoiceButton.onClick = [this]
    {
        audioProcessor.setTestSynthActive(testVoiceButton.getToggleState());
    };
    addAndMakeVisible(testVoiceButton);

    // Pitch / Auto-Tune Section Knobs
    setupRotaryKnob(retuneSlider,      retuneLabel,      "RETUNE SPEED", " ms");
    setupRotaryKnob(correctionSlider,  correctionLabel,  "CORRECTION",   " %");
    setupRotaryKnob(pitchShiftSlider,  pitchShiftLabel,  "PITCH SHIFT",  " st");

    // Separate Vocoder & Output Section Knobs
    setupRotaryKnob(vocoderMixSlider,  vocoderMixLabel,  "VOCODER MIX",  " %");
    setupRotaryKnob(vocoderToneSlider, vocoderToneLabel, "VOCODER TONE", " %");
    setupRotaryKnob(gainSlider,        gainLabel,        "OUTPUT GAIN",  " dB");

    pitchShiftLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc084fc));
    vocoderMixLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff43f5e));
    vocoderToneLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff43f5e));

    addAndMakeVisible(retuneSlider);      addAndMakeVisible(retuneLabel);
    addAndMakeVisible(correctionSlider);  addAndMakeVisible(correctionLabel);
    addAndMakeVisible(pitchShiftSlider);  addAndMakeVisible(pitchShiftLabel);
    addAndMakeVisible(vocoderMixSlider);  addAndMakeVisible(vocoderMixLabel);
    addAndMakeVisible(vocoderToneSlider); addAndMakeVisible(vocoderToneLabel);
    addAndMakeVisible(gainSlider);        addAndMakeVisible(gainLabel);

    retuneAttach      = std::make_unique<APVTS::SliderAttachment>(apvts, "retuneSpeed",      retuneSlider);
    correctionAttach  = std::make_unique<APVTS::SliderAttachment>(apvts, "correctionAmount", correctionSlider);
    pitchShiftAttach  = std::make_unique<APVTS::SliderAttachment>(apvts, "pitchShift",       pitchShiftSlider);
    vocoderMixAttach  = std::make_unique<APVTS::SliderAttachment>(apvts, "vocoderMix",       vocoderMixSlider);
    vocoderToneAttach = std::make_unique<APVTS::SliderAttachment>(apvts, "vocoderTone",      vocoderToneSlider);
    gainAttach        = std::make_unique<APVTS::SliderAttachment>(apvts, "outputGain",       gainSlider);

    startTimerHz(30);
}

AntiTuneAudioProcessorEditor::~AntiTuneAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void AntiTuneAudioProcessorEditor::timerCallback()
{
    const bool vocOn = audioProcessor.getAPVTS().getRawParameterValue("vocoderOn")->load() > 0.5f;
    vocoderMixSlider.setEnabled(vocOn);
    vocoderToneSlider.setEnabled(vocOn);
    repaint();
}

void AntiTuneAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    const auto pt = event.position;
    for (int i = 0; i < 12; ++i)
    {
        if (noteKeyRects[static_cast<size_t>(i)].contains(pt))
        {
            bool currentlyEnabled = audioProcessor.isNoteEnabled(i);
            audioProcessor.setNoteOverride(i, !currentlyEnabled);
            repaint();
            break;
        }
    }
}

void AntiTuneAudioProcessorEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient bgGrad(juce::Colour(0xff0b0e17), 0.0f, 0.0f,
                                juce::Colour(0xff131829), 0.0f, static_cast<float>(getHeight()), false);
    g.setGradientFill(bgGrad);
    g.fillAll();

    // Top Header Bar
    juce::Rectangle<float> headerRect(16.0f, 12.0f, static_cast<float>(getWidth() - 32), 54.0f);
    g.setColour(juce::Colour(0xff151b2e));
    g.fillRoundedRectangle(headerRect, 10.0f);
    g.setColour(juce::Colour(0xff25304d));
    g.drawRoundedRectangle(headerRect, 10.0f, 1.2f);

    g.setColour(juce::Colour(0xff00f0ff));
    g.setFont(juce::Font(juce::FontOptions(22.0f).withStyle("Bold")));
    g.drawText("ANTITUNE", 28, 18, 150, 26, juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xff94a3b8));
    g.setFont(juce::Font(juce::FontOptions(10.5f)));
    g.drawText("LIVE T-PAIN ENGINE + VOCODER", 28, 40, 175, 18, juce::Justification::centredLeft);

    // Visualizers section
    juce::Rectangle<float> gaugeBounds(16.0f, 78.0f, 280.0f, 225.0f);
    juce::Rectangle<float> graphBounds(308.0f, 78.0f, static_cast<float>(getWidth() - 324), 225.0f);

    drawLaserGauge(g, gaugeBounds);
    drawPitchHistoryGraph(g, graphBounds);

    // Interactive 12-note Scale Keyboard Strip
    keyboardBounds = juce::Rectangle<float>(16.0f, 314.0f, static_cast<float>(getWidth() - 32), 66.0f);
    drawScaleKeyboard(g, keyboardBounds);

    // Bottom Left Panel: PITCH CORRECTION & TRANSPOSE
    juce::Rectangle<float> pitchRackBounds(16.0f, 390.0f, 412.0f, 160.0f);
    g.setColour(juce::Colour(0xff14192b));
    g.fillRoundedRectangle(pitchRackBounds, 10.0f);
    g.setColour(juce::Colour(0xff00d4ff).withAlpha(0.35f));
    g.drawRoundedRectangle(pitchRackBounds, 10.0f, 1.2f);

    g.setColour(juce::Colour(0xff00f0ff));
    g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
    g.drawText("PITCH CORRECTION & VOICE SHIFT", 28, 396, 300, 16, juce::Justification::centredLeft);

    // Bottom Right Panel: SEPARATE STUDIO VOCODER & OUTPUT
    juce::Rectangle<float> vocRackBounds(440.0f, 390.0f, 404.0f, 160.0f);
    const bool vocOn = audioProcessor.getAPVTS().getRawParameterValue("vocoderOn")->load() > 0.5f;
    g.setColour(juce::Colour(0xff14192b));
    g.fillRoundedRectangle(vocRackBounds, 10.0f);
    g.setColour(vocOn ? juce::Colour(0xfff43f5e).withAlpha(0.65f) : juce::Colour(0xff232c47));
    g.drawRoundedRectangle(vocRackBounds, 10.0f, 1.2f);

    g.setColour(vocOn ? juce::Colour(0xfff43f5e) : juce::Colour(0xff64748b));
    g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
    g.drawText(vocOn ? "STUDIO VOCODER (ACTIVE) & OUTPUT" : "STUDIO VOCODER (BYPASSED - CLICK 'VOCODER ON' TOP)",
               452, 396, 380, 16, juce::Justification::centredLeft);
}

void AntiTuneAudioProcessorEditor::drawLaserGauge(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour(juce::Colour(0xff121728));
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setColour(juce::Colour(0xff232c47));
    g.drawRoundedRectangle(bounds, 10.0f, 1.2f);

    const auto& tel = audioProcessor.getTelemetry();
    const bool voiced = tel.isVoiced.load(std::memory_order_relaxed);
    const int targetMidi = tel.targetMidi.load(std::memory_order_relaxed);
    const float cents = tel.centsError.load(std::memory_order_relaxed);
    const float inHz = tel.inputFreqHz.load(std::memory_order_relaxed);
    const float targetHz = tel.targetFreqHz.load(std::memory_order_relaxed);
    const bool isLaser = audioProcessor.getAPVTS().getRawParameterValue("laserMode")->load() > 0.5f;
    const bool vocOn   = audioProcessor.getAPVTS().getRawParameterValue("vocoderOn")->load() > 0.5f;
    const float pitchSt = audioProcessor.getAPVTS().getRawParameterValue("pitchShift")->load();

    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY() + 8.0f;
    const float radius = 78.0f;

    const float startAngle = -2.2f;
    const float endAngle   =  2.2f;

    juce::Path bgArc;
    bgArc.addCentredArc(cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(juce::Colour(0xff1e263d));
    g.strokePath(bgArc, juce::PathStrokeType(8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (voiced && targetMidi > 0)
    {
        float normCents = std::clamp(cents / 50.0f, -1.0f, 1.0f);
        float errAngle = normCents * 2.2f;

        juce::Path errArc;
        errArc.addCentredArc(cx, cy, radius, radius, 0.0f,
                             std::min(0.0f, errAngle), std::max(0.0f, errAngle), true);
        g.setColour(vocOn ? juce::Colour(0xfff43f5e) : (isLaser ? juce::Colour(0xff00f0ff) : juce::Colour(0xffa855f7)));
        g.strokePath(errArc, juce::PathStrokeType(8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    int displayedMidi = targetMidi + static_cast<int>(std::llround(pitchSt));
    juce::String noteStr = (voiced && targetMidi > 0)
        ? juce::String(antitune::ScaleQuantizer::midiNoteToString(displayedMidi))
        : juce::String("--");

    g.setColour((voiced && targetMidi > 0) ? (isLaser ? juce::Colour(0xff00f0ff) : juce::Colours::white) : juce::Colour(0xff475569));
    g.setFont(juce::Font(juce::FontOptions(44.0f).withStyle("Bold")));
    g.drawText(noteStr, static_cast<int>(cx - 65), static_cast<int>(cy - 32), 130, 48, juce::Justification::centred);

    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.setColour(juce::Colour(0xff94a3b8));
    if (voiced && targetMidi > 0)
    {
        juce::String info = juce::String(inHz, 1) + " Hz -> " + juce::String(targetHz, 1) + " Hz";
        g.drawText(info, static_cast<int>(bounds.getX() + 10), static_cast<int>(bounds.getBottom() - 28),
                   static_cast<int>(bounds.getWidth() - 20), 20, juce::Justification::centred);
    }
    else
    {
        g.drawText("READY FOR LIVE MIC / TEST VOICE", static_cast<int>(bounds.getX() + 10),
                   static_cast<int>(bounds.getBottom() - 28), static_cast<int>(bounds.getWidth() - 20), 20,
                   juce::Justification::centred);
    }

    g.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
    g.setColour(vocOn ? juce::Colour(0xfff43f5e) : (isLaser ? juce::Colour(0xff00f0ff) : juce::Colour(0xffa855f7)));
    juce::String badge = isLaser ? "T-PAIN 0ms HARD SNAP" : "NATURAL STUDIO TUNE";
    if (vocOn)
        badge += " + VOCODER";
    g.drawText(badge,
               static_cast<int>(bounds.getX() + 12), static_cast<int>(bounds.getY() + 8),
               static_cast<int>(bounds.getWidth() - 24), 18, juce::Justification::centred);
}

void AntiTuneAudioProcessorEditor::drawPitchHistoryGraph(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour(juce::Colour(0xff101423));
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setColour(juce::Colour(0xff232c47));
    g.drawRoundedRectangle(bounds, 10.0f, 1.2f);

    const int numLines = 12;
    for (int i = 1; i < numLines; ++i)
    {
        float y = bounds.getY() + (bounds.getHeight() * static_cast<float>(i) / static_cast<float>(numLines));
        g.setColour(juce::Colour(0xff1b2238));
        g.drawHorizontalLine(static_cast<int>(y), bounds.getX() + 8.0f, bounds.getRight() - 8.0f);
    }

    std::array<AntiTuneAudioProcessor::HistoryPoint, AntiTuneAudioProcessor::kHistorySize> hist;
    int writePos = 0;
    audioProcessor.getPitchHistory(hist, writePos);

    float centerMidi = 57.0f;
    for (int i = 0; i < AntiTuneAudioProcessor::kHistorySize; ++i)
    {
        int idx = (writePos - 1 - i + AntiTuneAudioProcessor::kHistorySize) % AntiTuneAudioProcessor::kHistorySize;
        if (hist[static_cast<size_t>(idx)].voiced && hist[static_cast<size_t>(idx)].targetMidi > 20.0f)
        {
            centerMidi = hist[static_cast<size_t>(idx)].targetMidi;
            break;
        }
    }

    const float midiSpan = 14.0f;
    auto midiToY = [&](float midi) -> float
    {
        float norm = (midi - (centerMidi - midiSpan * 0.5f)) / midiSpan;
        norm = std::clamp(norm, 0.05f, 0.95f);
        return bounds.getBottom() - norm * bounds.getHeight();
    };

    juce::Path inputCurve;
    juce::Path laserTargetCurve;
    bool inStarted = false;
    bool targetStarted = false;

    const float padX = 12.0f;
    const float usableW = bounds.getWidth() - padX * 2.0f;

    for (int i = 0; i < AntiTuneAudioProcessor::kHistorySize; ++i)
    {
        int idx = (writePos + i) % AntiTuneAudioProcessor::kHistorySize;
        const auto& pt = hist[static_cast<size_t>(idx)];
        float x = bounds.getX() + padX + (static_cast<float>(i) / static_cast<float>(AntiTuneAudioProcessor::kHistorySize - 1)) * usableW;

        if (pt.voiced && pt.inputMidi > 20.0f)
        {
            float yIn = midiToY(pt.inputMidi);
            float yTg = midiToY(pt.targetMidi);

            if (!inStarted)
            {
                inputCurve.startNewSubPath(x, yIn);
                inStarted = true;
            }
            else
            {
                inputCurve.lineTo(x, yIn);
            }

            if (!targetStarted)
            {
                laserTargetCurve.startNewSubPath(x, yTg);
                targetStarted = true;
            }
            else
            {
                laserTargetCurve.lineTo(x, yTg);
            }
        }
        else
        {
            inStarted = false;
            targetStarted = false;
        }
    }

    g.setColour(juce::Colour(0xff64748b).withAlpha(0.65f));
    g.strokePath(inputCurve, juce::PathStrokeType(2.0f));

    g.setColour(juce::Colour(0xff00f0ff).withAlpha(0.25f));
    g.strokePath(laserTargetCurve, juce::PathStrokeType(6.0f));
    g.setColour(juce::Colour(0xff00f0ff));
    g.strokePath(laserTargetCurve, juce::PathStrokeType(2.6f));

    g.setFont(juce::Font(juce::FontOptions(11.5f).withStyle("Bold")));
    g.setColour(juce::Colour(0xff94a3b8));
    g.drawText("LIVE PITCH TRACKER  (0.62 SEMITONE HYSTERESIS = ZERO NOTE FLUTTER)",
               static_cast<int>(bounds.getX() + 14), static_cast<int>(bounds.getY() + 8),
               static_cast<int>(bounds.getWidth() - 28), 18, juce::Justification::centredLeft);
}

void AntiTuneAudioProcessorEditor::drawScaleKeyboard(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour(juce::Colour(0xff14192b));
    g.fillRoundedRectangle(bounds, 10.0f);
    g.setColour(juce::Colour(0xff232c47));
    g.drawRoundedRectangle(bounds, 10.0f, 1.2f);

    const auto mask = audioProcessor.getActiveScaleMask();
    const auto& tel = audioProcessor.getTelemetry();
    const bool voiced = tel.isVoiced.load(std::memory_order_relaxed);
    const int targetMidi = tel.targetMidi.load(std::memory_order_relaxed);
    const int activePitchClass = (voiced && targetMidi > 0) ? ((targetMidi % 12) + 12) % 12 : -1;

    const float pad = 10.0f;
    const float gap = 6.0f;
    const float totalW = bounds.getWidth() - pad * 2.0f;
    const float keyW = (totalW - gap * 11.0f) / 12.0f;
    const float keyH = bounds.getHeight() - 16.0f;

    for (int i = 0; i < 12; ++i)
    {
        float kx = bounds.getX() + pad + static_cast<float>(i) * (keyW + gap);
        float ky = bounds.getY() + 8.0f;
        juce::Rectangle<float> r(kx, ky, keyW, keyH);
        noteKeyRects[static_cast<size_t>(i)] = r;

        const bool enabled = mask[static_cast<size_t>(i)];
        const bool isPlaying = (i == activePitchClass);

        if (isPlaying)
            g.setColour(juce::Colour(0xff00f0ff));
        else if (enabled)
            g.setColour(juce::Colour(0xff1e293b));
        else
            g.setColour(juce::Colour(0xff0d111a));

        g.fillRoundedRectangle(r, 6.0f);

        g.setColour(isPlaying ? juce::Colours::white
                              : (enabled ? juce::Colour(0xff00d4ff).withAlpha(0.6f) : juce::Colour(0xff1e2638)));
        g.drawRoundedRectangle(r, 6.0f, isPlaying ? 2.0f : 1.2f);

        g.setColour(isPlaying ? juce::Colour(0xff090d16)
                              : (enabled ? juce::Colours::white : juce::Colour(0xff475569)));
        g.setFont(juce::Font(juce::FontOptions(13.5f).withStyle("Bold")));
        g.drawText(antitune::ScaleQuantizer::kNoteNames[i], r.toNearestInt(), juce::Justification::centred);
    }
}

void AntiTuneAudioProcessorEditor::resized()
{
    // Top bar controls
    keyCombo.setBounds(206, 23, 66, 32);
    scaleCombo.setBounds(278, 23, 206, 32);
    laserModeButton.setBounds(492, 23, 132, 32);
    vocoderOnButton.setBounds(628, 23, 112, 32);
    testVoiceButton.setBounds(742, 23, 98, 32);

    const int rackY = 416;
    const int knobW = 124;
    const int knobH = 108;

    // Left Section: 3 Pitch Knobs (Retune Speed, Correction Strength, Pitch Shift)
    auto placeLeftKnob = [&](int index, juce::Slider& s, juce::Label& l)
    {
        int x = 26 + index * (knobW + 8);
        l.setBounds(x, rackY, knobW, 18);
        s.setBounds(x, rackY + 16, knobW, knobH);
    };

    placeLeftKnob(0, retuneSlider,     retuneLabel);
    placeLeftKnob(1, correctionSlider, correctionLabel);
    placeLeftKnob(2, pitchShiftSlider, pitchShiftLabel);

    // Right Section: 3 Vocoder & Output Knobs (Vocoder Mix, Vocoder Tone, Output Gain)
    auto placeRightKnob = [&](int index, juce::Slider& s, juce::Label& l)
    {
        int x = 450 + index * (knobW + 6);
        l.setBounds(x, rackY, knobW, 18);
        s.setBounds(x, rackY + 16, knobW, knobH);
    };

    placeRightKnob(0, vocoderMixSlider,  vocoderMixLabel);
    placeRightKnob(1, vocoderToneSlider, vocoderToneLabel);
    placeRightKnob(2, gainSlider,        gainLabel);
}
