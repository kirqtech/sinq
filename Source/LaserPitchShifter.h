#pragma once

#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

namespace antitune
{

// Clean, non-ringing 2nd-order Butterworth/SVF Bandpass for the Studio Vocoder
struct CleanVocoderBand
{
    float ic1eq = 0.0f;
    float ic2eq = 0.0f;
    float g = 0.1f;
    float k = 0.35f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float a3 = 0.0f;

    void setCoefficients(float sampleRate, float centerFreqHz, float Q)
    {
        float fc = std::clamp(centerFreqHz, 70.0f, sampleRate * 0.42f);
        g = std::tan(3.14159265f * fc / sampleRate);
        k = 1.0f / std::max(0.7f, Q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset()
    {
        ic1eq = 0.0f;
        ic2eq = 0.0f;
    }

    inline float processBandpass(float v0)
    {
        float v3 = v0 - ic2eq;
        float v1 = a1 * ic1eq + a2 * v3;
        float v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0f * v1 - ic1eq;
        ic2eq = 2.0f * v2 - ic2eq;
        return v1 * k;
    }
};

class LaserPitchShifter
{
public:
    static constexpr int kNumVocoderBands = 20;

    void prepare(double sampleRate, int channelIndex = 0)
    {
        sr = static_cast<float>(sampleRate);
        chIdx = channelIndex;

        // 16384-sample circular buffer matching VocalSnap
        bufferSize = 16384;
        bufferMask = bufferSize - 1;
        delayBuffer.assign(static_cast<size_t>(bufferSize), 0.0f);

        writePos = 0;
        phaseA = 0.0f;

        // Exact VocalSnap grain initialization (20 ms)
        float initGrain = static_cast<float>(sampleRate * 0.020);
        currentGrainLen = initGrain;
        targetGrainLen  = initGrain;

        // Vocoder state
        oscPhase1 = (channelIndex == 1) ? 0.25f : 0.0f;
        oscPhase2 = (channelIndex == 1) ? 0.62f : 0.37f;
        oscSubPhase = 0.0f;
        smoothedCarrierHz = 220.0f;
        vocoderEnvLevel = 0.0f;
        hpStateX = 0.0f;
        hpStateY = 0.0f;

        setupVocoderBands();
    }

    void reset()
    {
        std::fill(delayBuffer.begin(), delayBuffer.end(), 0.0f);
        writePos = 0;
        phaseA = 0.0f;
        for (int b = 0; b < kNumVocoderBands; ++b)
        {
            modBands1[static_cast<size_t>(b)].reset();
            modBands2[static_cast<size_t>(b)].reset();
            carBands1[static_cast<size_t>(b)].reset();
            carBands2[static_cast<size_t>(b)].reset();
            bandEnvelopes[static_cast<size_t>(b)] = 0.0f;
        }
    }

    int getLatencySamples() const
    {
        float initGrain = sr * 0.020f;
        return static_cast<int>(std::llround(0.5f * initGrain + 4.0f));
    }

    // Updates pitch-synchronous target grain length on each YIN hop (exact VocalSnap formula!)
    void updateTargetGrainFromPitch(float detectedHz)
    {
        if (detectedHz > 60.0f && detectedHz < 1100.0f)
        {
            float periodSamples = sr / detectedHz;
            float idealSamples  = sr * 0.018f; // 18 ms ideal window
            float numPeriods    = std::ceil(idealSamples / periodSamples);
            float syncedLen     = numPeriods * periodSamples;

            float minLen = sr * 0.012f; // 12 ms min
            float maxLen = sr * 0.048f; // 48 ms max
            targetGrainLen = std::clamp(syncedLen, minLen, maxLen);
        }
    }

    // Core sample processor:
    // 1. Runs VocalSnap's exact Phase-Coupled Dual-Hermite Pitch Shifter (zero chopping!)
    // 2. Optionally runs the separate Studio 20-Band Vocoder when vocoderEnabled == true
    float processSample(float input,
                        float pitchRatio,
                        float grainSmoothAlpha,
                        bool vocoderEnabled,
                        float vocoderMix,
                        float vocoderTone,
                        float targetCarrierHz,
                        bool isVoiced)
    {
        // Smoothly update pitch-synchronous grain length
        currentGrainLen += grainSmoothAlpha * (targetGrainLen - currentGrainLen);

        // Write input sample into circular buffer
        delayBuffer[static_cast<size_t>(writePos)] = input;

        // =========================================================================
        // 1. VOCALSNAP PHASE-COUPLED DUAL-HERMITE PITCH SHIFTER (100% Smooth Live Engine)
        // =========================================================================
        float phaseB = phaseA + 0.5f;
        if (phaseB >= 1.0f)
            phaseB -= 1.0f;

        // Complementary Hann window: winA is strictly 0.0 at phaseA == 0.0,
        // and winB is strictly 0.0 at phaseB == 0.0!
        float winA = 0.5f - 0.5f * std::cos(6.283185307f * phaseA);
        float winB = 1.0f - winA;

        // Direct phase-coupled read delays:
        // Delay wraps around ONLY when its corresponding window is 0.000000!
        float delayA = phaseA * currentGrainLen + 4.0f;
        float delayB = phaseB * currentGrainLen + 4.0f;

        float sampleA = readHermite(delayA);
        float sampleB = readHermite(delayB);

        float tunedVocal = winA * sampleA + winB * sampleB;

        // Advance phaseA by (1.0 - pitchRatio) / currentGrainLen and wrap to [0, 1)
        float nextPhase = phaseA + (1.0f - pitchRatio) / currentGrainLen;
        nextPhase -= std::floor(nextPhase);
        phaseA = nextPhase;

        writePos = (writePos + 1) & bufferMask;

        // If the separate Vocoder is OFF, return the pure, silky T-Pain / VocalSnap vocal immediately!
        if (!vocoderEnabled || vocoderMix <= 0.001f)
        {
            return tunedVocal;
        }

        // =========================================================================
        // 2. SEPARATE STUDIO 20-BAND HARMONIC VOCODER (Clean, Non-Choppy Synth Voice)
        // =========================================================================
        if (targetCarrierHz > 50.0f)
        {
            // Fast carrier pitch tracking so vocoder follows the Auto-Tuned notes smoothly
            const float carAlpha = 1.0f - std::exp(-1.0f / (0.0025f * sr));
            smoothedCarrierHz += carAlpha * (targetCarrierHz - smoothedCarrierHz);
        }

        // Track overall vocal envelope for clean gating
        float absVocal = std::abs(tunedVocal);
        float envCoeff = (absVocal > vocoderEnvLevel) ? 0.02f : 0.001f;
        vocoderEnvLevel += envCoeff * (absVocal - vocoderEnvLevel);

        // Stereo detune (+4 cents on Right channel, -4 cents on Left channel for wide studio sound)
        float stereoDetune = (chIdx == 1) ? 1.0023f : 0.9977f;
        float dt1 = std::clamp((smoothedCarrierHz * stereoDetune) / sr, 0.0005f, 0.45f);
        float dt2 = std::clamp((smoothedCarrierHz * (stereoDetune * 1.003f)) / sr, 0.0005f, 0.45f);
        float dtSub = dt1 * 0.5f;

        oscPhase1 += dt1;
        if (oscPhase1 >= 1.0f) oscPhase1 -= 1.0f;

        oscPhase2 += dt2;
        if (oscPhase2 >= 1.0f) oscPhase2 -= 1.0f;

        oscSubPhase += dtSub;
        if (oscSubPhase >= 1.0f) oscSubPhase -= 1.0f;

        // Band-limited PolyBLEP Glottal Saw + Warm Sub-Octave + Variable Pulse
        float saw1 = polyBlepSaw(oscPhase1, dt1);
        float saw2 = polyBlepSaw(oscPhase2, dt2);
        float pulseWidth = 0.28f + 0.15f * vocoderTone;
        float pulse = saw1 - polyBlepSaw(std::fmod(oscPhase1 + pulseWidth, 1.0f), dt1);
        float subSine = std::sin(6.283185307f * oscSubPhase);

        // Vocoder Tone blends between warm Talkbox/Pad (0%) and bright Synth Lead (100%)
        float carrier = (0.38f + 0.20f * vocoderTone) * saw1
                      + (0.28f + 0.15f * vocoderTone) * saw2
                      + (0.25f * vocoderTone) * pulse
                      + (0.32f * (1.0f - 0.4f * vocoderTone)) * subSine;

        // Smooth envelope followers (5 ms attack, 38 ms release -> zero gargling or chopping!)
        const float envAttack  = 1.0f - std::exp(-1.0f / (0.005f * sr));
        const float envRelease = 1.0f - std::exp(-1.0f / (0.038f * sr));

        float vocoderSum = 0.0f;
        for (int b = 0; b < kNumVocoderBands; ++b)
        {
            const size_t bi = static_cast<size_t>(b);

            // Analyze the already pitch-corrected vocal formants
            float modBand = modBands1[bi].processBandpass(tunedVocal);
            modBand = modBands2[bi].processBandpass(modBand);

            float level = std::abs(modBand);
            float env = bandEnvelopes[bi];
            env += ((level > env) ? envAttack : envRelease) * (level - env);
            bandEnvelopes[bi] = env;

            // Filter synth carrier through matching band
            float carBand = carBands1[bi].processBandpass(carrier);
            carBand = carBands2[bi].processBandpass(carBand);

            float tilt = bandGains[bi];
            vocoderSum += carBand * env * tilt;
        }

        // Preserve natural crisp consonants/diction above 3.6 kHz from the tuned vocal
        const float hpAlpha = 0.84f;
        float crispConsonants = hpAlpha * (hpStateY + tunedVocal - hpStateX);
        hpStateX = tunedVocal;
        hpStateY = crispConsonants;

        // Gentle studio leveling (no harsh distortion!)
        float cleanVocoder = (vocoderSum * 4.2f) / (1.0f + std::abs(vocoderSum * 2.2f))
                           + crispConsonants * (isVoiced ? 0.22f : 0.55f);

        return tunedVocal * (1.0f - vocoderMix) + cleanVocoder * vocoderMix;
    }

private:
    void setupVocoderBands()
    {
        const float minF = 100.0f;
        const float maxF = 8200.0f;
        // Moderate Q = 3.2 gives smooth, overlapping formant bands without metallic ringing
        const float Q = 3.2f;

        for (int b = 0; b < kNumVocoderBands; ++b)
        {
            const size_t bi = static_cast<size_t>(b);
            float t = static_cast<float>(b) / static_cast<float>(kNumVocoderBands - 1);
            float fc = minF * std::pow(maxF / minF, t);

            modBands1[bi].setCoefficients(sr, fc, Q);
            modBands2[bi].setCoefficients(sr, fc, Q);
            carBands1[bi].setCoefficients(sr, fc, Q);
            carBands2[bi].setCoefficients(sr, fc, Q);
            bandEnvelopes[bi] = 0.0f;
            // Gentle high-frequency presence compensation
            bandGains[bi] = 1.0f + 0.09f * static_cast<float>(b);
        }
    }

    static inline float polyBlepSaw(float phase, float dt)
    {
        float v = 2.0f * phase - 1.0f;
        if (phase < dt)
        {
            float t = phase / dt;
            v -= (t + t - t * t - 1.0f);
        }
        else if (phase > 1.0f - dt)
        {
            float t = (phase - 1.0f) / dt;
            v -= (t * t + t + t + 1.0f);
        }
        return v;
    }

    // Exact 4-point Hermite cubic interpolation matching VocalSnap (lines 29108-29138)
    inline float readHermite(float delaySamples) const
    {
        float readPos = static_cast<float>(writePos) - delaySamples;
        float floorPos = std::floor(readPos);
        int baseIdx = static_cast<int>(floorPos);
        float frac = readPos - floorPos;

        int idx0 = (baseIdx - 1) & bufferMask;
        int idx1 = (baseIdx)     & bufferMask;
        int idx2 = (baseIdx + 1) & bufferMask;
        int idx3 = (baseIdx + 2) & bufferMask;

        float y0 = delayBuffer[static_cast<size_t>(idx0)];
        float y1 = delayBuffer[static_cast<size_t>(idx1)];
        float y2 = delayBuffer[static_cast<size_t>(idx2)];
        float y3 = delayBuffer[static_cast<size_t>(idx3)];

        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + y1;
    }

    float sr = 44100.0f;
    int chIdx = 0;
    int bufferSize = 16384;
    int bufferMask = 16383;
    int writePos = 0;

    float phaseA = 0.0f;
    float currentGrainLen = 882.0f;
    float targetGrainLen  = 882.0f;

    float oscPhase1 = 0.0f;
    float oscPhase2 = 0.37f;
    float oscSubPhase = 0.0f;
    float smoothedCarrierHz = 220.0f;
    float vocoderEnvLevel = 0.0f;
    float hpStateX = 0.0f;
    float hpStateY = 0.0f;

    std::vector<float> delayBuffer;

    std::array<CleanVocoderBand, kNumVocoderBands> modBands1;
    std::array<CleanVocoderBand, kNumVocoderBands> modBands2;
    std::array<CleanVocoderBand, kNumVocoderBands> carBands1;
    std::array<CleanVocoderBand, kNumVocoderBands> carBands2;
    std::array<float, kNumVocoderBands> bandEnvelopes {};
    std::array<float, kNumVocoderBands> bandGains {};
};

} // namespace antitune
