#include "PluginProcessor.h"
#include "PluginEditor.h"

AntiTuneAudioProcessor::AntiTuneAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (auto& ov : noteOverrides)
        ov.store(-1);
}

AntiTuneAudioProcessor::~AntiTuneAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout AntiTuneAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    juce::StringArray keyNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "rootKey", 1 }, "Root Key", keyNames, 0));

    juce::StringArray scaleNames {
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
    };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "scaleType", 1 }, "Scale", scaleNames, 0));

    // T-Pain 0ms Hard Mode Toggle
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "laserMode", 1 }, "T-Pain 0ms Mode", true));

    // Retune Speed: 1.0 ms to 200.0 ms
    juce::NormalisableRange<float> retuneRange(0.0f, 200.0f, 0.1f, 0.45f);
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "retuneSpeed", 1 }, "Retune Speed", retuneRange, 0.0f));

    // Pitch Correction Strength (0% to 100% - controls how strongly notes are pulled to scale)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "correctionAmount", 1 }, "Correction Amount",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.5f), 100.0f));

    // Manual Voice Pitch Shift (-12.0 to +12.0 semitones - raise or lower voice pitch!)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "pitchShift", 1 }, "Pitch Shift",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f), 0.0f));

    // Separate Vocoder On/Off Switch (Off by default so pure T-Pain Auto-Tune plays cleanly!)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "vocoderOn", 1 }, "Vocoder On", false));

    // Vocoder Mix (0% to 100% when Vocoder is enabled)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "vocoderMix", 1 }, "Vocoder Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.5f), 70.0f));

    // Vocoder Tone / Synth Character (0% Warm Talkbox to 100% Bright Lead)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "vocoderTone", 1 }, "Vocoder Tone",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.5f), 55.0f));

    // Output Gain (-24 dB to +12 dB)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "outputGain", 1 }, "Output Gain",
        juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f));

    return { params.begin(), params.end() };
}

const juce::String AntiTuneAudioProcessor::getName() const { return JucePlugin_Name; }
bool AntiTuneAudioProcessor::acceptsMidi() const { return false; }
bool AntiTuneAudioProcessor::producesMidi() const { return false; }
bool AntiTuneAudioProcessor::isMidiEffect() const { return false; }
double AntiTuneAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int AntiTuneAudioProcessor::getNumPrograms() { return 1; }
int AntiTuneAudioProcessor::getCurrentProgram() { return 0; }
void AntiTuneAudioProcessor::setCurrentProgram(int) {}
const juce::String AntiTuneAudioProcessor::getProgramName(int) { return {}; }
void AntiTuneAudioProcessor::changeProgramName(int, const juce::String&) {}

void AntiTuneAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    pitchDetector.prepare(sampleRate);
    shifterL.prepare(sampleRate, 0);
    shifterR.prepare(sampleRate, 1);

    currentTargetNote     = -1;
    unvoicedHopCount      = 0;
    currentShiftSemitones = 0.0f;
    targetShiftSemitones  = 0.0f;
    lastDetectedMidi      = -1.0f;
    historySampleCounter  = 0;

    setLatencySamples(shifterL.getLatencySamples());
}

void AntiTuneAudioProcessor::releaseResources()
{
    shifterL.reset();
    shifterR.reset();
}

bool AntiTuneAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& outSet = layouts.getMainOutputChannelSet();
    const auto& inSet  = layouts.getMainInputChannelSet();

    if (outSet != juce::AudioChannelSet::mono() && outSet != juce::AudioChannelSet::stereo())
        return false;

    if (inSet != juce::AudioChannelSet::mono() && inSet != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

std::array<bool, 12> AntiTuneAudioProcessor::getActiveScaleMask() const
{
    int rootKey = static_cast<int>(apvts.getRawParameterValue("rootKey")->load());
    int scaleChoice = static_cast<int>(apvts.getRawParameterValue("scaleType")->load());

    antitune::ScaleType st = static_cast<antitune::ScaleType>(std::clamp(scaleChoice, 0, 9));
    auto mask = antitune::ScaleQuantizer::getScaleMask(st, rootKey);

    for (int i = 0; i < 12; ++i)
    {
        int ov = noteOverrides[static_cast<size_t>(i)].load();
        if (ov == 0) mask[static_cast<size_t>(i)] = false;
        else if (ov == 1) mask[static_cast<size_t>(i)] = true;
    }
    return mask;
}

void AntiTuneAudioProcessor::setNoteOverride(int pitchClass, bool enabled)
{
    if (pitchClass >= 0 && pitchClass < 12)
        noteOverrides[static_cast<size_t>(pitchClass)].store(enabled ? 1 : 0);
}

bool AntiTuneAudioProcessor::isNoteEnabled(int pitchClass) const
{
    auto mask = getActiveScaleMask();
    return mask[static_cast<size_t>((pitchClass % 12 + 12) % 12)];
}

void AntiTuneAudioProcessor::clearNoteOverrides()
{
    for (auto& ov : noteOverrides)
        ov.store(-1);
}

void AntiTuneAudioProcessor::getPitchHistory(std::array<HistoryPoint, kHistorySize>& outHistory, int& outWritePos) const
{
    outWritePos = historyWritePos.load(std::memory_order_relaxed);
    outHistory = pitchHistory;
}

void AntiTuneAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples             = buffer.getNumSamples();
    const float sr                   = static_cast<float>(getSampleRate());

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);

    if (totalNumInputChannels == 0 || numSamples <= 0)
        return;

    const bool tPainHardMode    = apvts.getRawParameterValue("laserMode")->load() > 0.5f;
    const float rawRetuneMs     = apvts.getRawParameterValue("retuneSpeed")->load();
    // Exact VocalSnap time constant: 1.0 ms in Hard Snap mode, or max(1.0, retuneSpeed) in natural mode
    const float effectiveRetune = tPainHardMode ? 1.0f : std::max(1.0f, rawRetuneMs);

    const float corrStrength    = apvts.getRawParameterValue("correctionAmount")->load() * 0.01f;
    const float manualPitchSt   = apvts.getRawParameterValue("pitchShift")->load();

    const bool vocoderEnabled   = apvts.getRawParameterValue("vocoderOn")->load() > 0.5f;
    const float vocoderMix      = apvts.getRawParameterValue("vocoderMix")->load() * 0.01f;
    const float vocoderTone     = apvts.getRawParameterValue("vocoderTone")->load() * 0.01f;

    const float gainDb          = apvts.getRawParameterValue("outputGain")->load();
    const float linearGain      = (gainDb <= -23.9f) ? 0.0f : std::pow(10.0f, gainDb * 0.05f);

    // Exact VocalSnap per-sample smoothing coefficients (lines 28520-28545)
    const float retuneAlpha  = 1.0f - std::exp(-1.0f / (effectiveRetune * 0.001f * sr));
    const float releaseAlpha = 1.0f - std::exp(-1.0f / (0.030f * sr)); // 30 ms unvoiced release
    const float grainAlpha   = 1.0f - std::exp(-1.0f / (0.150f * sr)); // 150 ms smooth grain tracking

    const auto scaleMask = getActiveScaleMask();

    float* ch0 = buffer.getWritePointer(0);
    float* ch1 = (totalNumOutputChannels > 1) ? buffer.getWritePointer(1) : nullptr;
    const bool hasStereo = (totalNumInputChannels > 1 && ch1 != nullptr);

    float inSumSq = 0.0f;
    float outSumSq = 0.0f;

    const bool runDemoVoice = testSynthActive.load();
    const int historyStepSamples = std::max(128, static_cast<int>(sr / 60.0f));

    for (int i = 0; i < numSamples; ++i)
    {
        float inL = ch0[i];
        float inR = hasStereo ? ch1[i] : inL;

        if (runDemoVoice)
        {
            testLfoPhase += 0.32f / sr;
            if (testLfoPhase >= 1.0f) testLfoPhase -= 1.0f;

            float sweepMidi = 55.0f + 5.0f * std::sin(6.283185307f * testLfoPhase)
                            + 0.18f * std::sin(6.283185307f * testLfoPhase * 13.0f);
            float demoFreq = antitune::ScaleQuantizer::midiToFreq(sweepMidi);

            testCarrierPhase += demoFreq / sr;
            if (testCarrierPhase >= 1.0f) testCarrierPhase -= 1.0f;

            float p = 6.283185307f * testCarrierPhase;
            float glottal = 0.48f * std::sin(p)
                          + 0.28f * std::sin(2.0f * p)
                          + 0.18f * std::sin(3.0f * p)
                          + 0.10f * std::sin(4.0f * p);
            inL = glottal * 0.30f;
            inR = inL;
        }

        float monoIn = 0.5f * (inL + inR);
        inSumSq += monoIn * monoIn;

        // 1. Run 2x-decimated YIN pitch detector (triggers every ~5.8 ms hop)
        antitune::PitchDetectionResult det;
        if (pitchDetector.pushSample(monoIn, det))
        {
            if (det.isVoiced && det.frequencyHz > 60.0f)
            {
                unvoicedHopCount = 0;

                float continuousMidi = antitune::ScaleQuantizer::freqToMidi(det.frequencyHz);
                lastDetectedMidi = continuousMidi;

                // Quantize with VocalSnap's 0.62-semitone hysteresis so notes never flutter!
                currentTargetNote = antitune::ScaleQuantizer::quantizeMidiNote(
                    continuousMidi, scaleMask, currentTargetNote);

                // Target shift = Correction Strength * (TargetNote - InputMidi) + Manual Pitch Shift
                float autoTuneDelta = corrStrength * (static_cast<float>(currentTargetNote) - continuousMidi);
                targetShiftSemitones = autoTuneDelta + manualPitchSt;

                // Update pitch-synchronous grain window length on both channels
                shifterL.updateTargetGrainFromPitch(det.frequencyHz);
                shifterR.updateTargetGrainFromPitch(det.frequencyHz);

                float targetHz = antitune::ScaleQuantizer::midiToFreq(
                    static_cast<float>(currentTargetNote) + manualPitchSt);
                float centsErr = (continuousMidi - static_cast<float>(currentTargetNote)) * 100.0f;

                telemetry.inputFreqHz.store(det.frequencyHz, std::memory_order_relaxed);
                telemetry.targetFreqHz.store(targetHz, std::memory_order_relaxed);
                telemetry.inputMidi.store(continuousMidi, std::memory_order_relaxed);
                telemetry.targetMidi.store(currentTargetNote, std::memory_order_relaxed);
                telemetry.centsError.store(centsErr, std::memory_order_relaxed);
                telemetry.isVoiced.store(true, std::memory_order_relaxed);
            }
            else
            {
                // Hold pitch across up to 3 unvoiced hops (~18 ms) so consonants never chop the note!
                if (++unvoicedHopCount > 3)
                {
                    targetShiftSemitones = manualPitchSt;
                    currentTargetNote = -1;
                    lastDetectedMidi  = -1.0f;
                    telemetry.isVoiced.store(false, std::memory_order_relaxed);
                    telemetry.inputMidi.store(-1.0f, std::memory_order_relaxed);
                    telemetry.targetMidi.store(-1, std::memory_order_relaxed);
                }
            }
        }

        // 2. Smoothly update currentShiftSemitones per sample (exact VocalSnap law!)
        float activeAlpha = (unvoicedHopCount < 4) ? retuneAlpha : releaseAlpha;
        currentShiftSemitones += activeAlpha * (targetShiftSemitones - currentShiftSemitones);

        float pitchRatio = std::exp2(currentShiftSemitones / 12.0f);

        float vocoderCarrierHz = (currentTargetNote > 0)
            ? antitune::ScaleQuantizer::midiToFreq(static_cast<float>(currentTargetNote) + manualPitchSt)
            : 220.0f;

        bool isCurrentlyVoiced = (unvoicedHopCount < 4);

        // 3. Process Left and Right channels through Phase-Coupled Shifter (+ optional Vocoder)
        float outL = shifterL.processSample(
            inL, pitchRatio, grainAlpha, vocoderEnabled, vocoderMix, vocoderTone, vocoderCarrierHz, isCurrentlyVoiced);

        ch0[i] = outL * linearGain;

        float outR = outL;
        if (hasStereo)
        {
            outR = shifterR.processSample(
                inR, pitchRatio, grainAlpha, vocoderEnabled, vocoderMix, vocoderTone, vocoderCarrierHz, isCurrentlyVoiced);
            ch1[i] = outR * linearGain;
        }
        else if (ch1 != nullptr)
        {
            ch1[i] = outL * linearGain;
        }

        outSumSq += 0.5f * (outL * outL + outR * outR) * (linearGain * linearGain);

        if (++historySampleCounter >= historyStepSamples)
        {
            historySampleCounter = 0;
            int wPos = historyWritePos.load(std::memory_order_relaxed);
            pitchHistory[static_cast<size_t>(wPos)] = {
                lastDetectedMidi,
                static_cast<float>(currentTargetNote),
                isCurrentlyVoiced && (currentTargetNote > 0)
            };
            historyWritePos.store((wPos + 1) % kHistorySize, std::memory_order_relaxed);
            telemetry.currentShiftSt.store(currentShiftSemitones, std::memory_order_relaxed);
        }
    }

    telemetry.inputRms.store(std::sqrt(inSumSq / static_cast<float>(numSamples)), std::memory_order_relaxed);
    telemetry.outputRms.store(std::sqrt(outSumSq / static_cast<float>(numSamples)), std::memory_order_relaxed);
}

bool AntiTuneAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* AntiTuneAudioProcessor::createEditor()
{
    return new AntiTuneAudioProcessorEditor(*this);
}

void AntiTuneAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AntiTuneAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AntiTuneAudioProcessor();
}
