#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include "PitchDetector.h"
#include "ScaleQuantizer.h"
#include "LaserPitchShifter.h"

class AntiTuneAudioProcessor : public juce::AudioProcessor
{
public:
    AntiTuneAudioProcessor();
    ~AntiTuneAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    struct Telemetry
    {
        std::atomic<float> inputFreqHz { 0.0f };
        std::atomic<float> targetFreqHz { 0.0f };
        std::atomic<float> inputMidi { -1.0f };
        std::atomic<int>   targetMidi { -1 };
        std::atomic<float> centsError { 0.0f };
        std::atomic<float> currentShiftSt { 0.0f };
        std::atomic<bool>  isVoiced { false };
        std::atomic<float> inputRms { 0.0f };
        std::atomic<float> outputRms { 0.0f };
    };

    Telemetry& getTelemetry() { return telemetry; }

    void setNoteOverride(int pitchClass, bool enabled);
    bool isNoteEnabled(int pitchClass) const;
    void clearNoteOverrides();
    std::array<bool, 12> getActiveScaleMask() const;

    void setTestSynthActive(bool active) { testSynthActive.store(active); }
    bool isTestSynthActive() const { return testSynthActive.load(); }

    static constexpr int kHistorySize = 180;
    struct HistoryPoint
    {
        float inputMidi  = 0.0f;
        float targetMidi = 0.0f;
        bool  voiced     = false;
    };

    void getPitchHistory(std::array<HistoryPoint, kHistorySize>& outHistory, int& outWritePos) const;

private:
    juce::AudioProcessorValueTreeState apvts;

    antitune::PitchDetector pitchDetector;
    antitune::LaserPitchShifter shifterL;
    antitune::LaserPitchShifter shifterR;

    Telemetry telemetry;

    std::array<std::atomic<int>, 12> noteOverrides;
    int currentTargetNote = -1;
    int unvoicedHopCount  = 0;
    float currentShiftSemitones = 0.0f;
    float targetShiftSemitones  = 0.0f;
    float lastDetectedMidi      = -1.0f;

    std::atomic<bool> testSynthActive { false };
    float testLfoPhase = 0.0f;
    float testCarrierPhase = 0.0f;

    std::array<HistoryPoint, kHistorySize> pitchHistory {};
    std::atomic<int> historyWritePos { 0 };
    int historySampleCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AntiTuneAudioProcessor)
};
