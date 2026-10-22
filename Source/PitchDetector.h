#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

namespace antitune
{

struct PitchDetectionResult
{
    float frequencyHz = 0.0f;
    float confidence  = 0.0f;
    bool  isVoiced    = false;
    float rmsLevel    = 0.0f;
};

// Exact 2x-Decimated YIN Pitch Detector modeled after VocalSnap's rock-solid live engine
class PitchDetector
{
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        decimatedSr = 0.5 * sampleRate;

        yinWindow = static_cast<int>(std::llround(decimatedSr * 0.022));
        minTau    = std::max(2, static_cast<int>(std::floor(decimatedSr / 1000.0)));
        maxTau    = static_cast<int>(std::ceil(decimatedSr / 70.0));
        analysisLen = yinWindow + maxTau + 2;
        hopSamples  = std::max(64, static_cast<int>(std::llround(sampleRate * 0.0058)));

        decimRing.assign(16384, 0.0f);
        ringMask = static_cast<int>(decimRing.size()) - 1;

        analysisBuf.assign(static_cast<size_t>(analysisLen), 0.0f);
        yinBuf.assign(static_cast<size_t>(maxTau + 2), 1.0f);

        decimWritePos = 0;
        decimPairCount = 0;
        hopCounter = 0;
        decimAccum = 0.0f;

        lastResult = {};
    }

    // Returns true on the sample where a new hop analysis was just completed
    bool pushSample(float monoSample, PitchDetectionResult& outResult)
    {
        // 2x Decimation (averages pairs of samples -> natural low-pass filter + 4x faster YIN)
        decimAccum += monoSample;
        if (++decimPairCount == 2)
        {
            decimRing[static_cast<size_t>(decimWritePos)] = decimAccum * 0.5f;
            decimWritePos = (decimWritePos + 1) & ringMask;
            decimAccum = 0.0f;
            decimPairCount = 0;
        }

        if (++hopCounter >= hopSamples)
        {
            hopCounter = 0;
            lastResult = runYin();
            outResult = lastResult;
            return true;
        }

        outResult = lastResult;
        return false;
    }

    PitchDetectionResult getLatestResult() const { return lastResult; }

private:
    PitchDetectionResult runYin()
    {
        PitchDetectionResult res;

        // Copy latest analysisLen decimated samples into contiguous buffer
        for (int i = 0; i < analysisLen; ++i)
        {
            int idx = (decimWritePos - analysisLen + i) & ringMask;
            analysisBuf[static_cast<size_t>(i)] = decimRing[static_cast<size_t>(idx)];
        }

        // Compute RMS over the most recent yinWindow samples
        double sumSq = 0.0;
        const int rmsStart = analysisLen - yinWindow;
        for (int i = rmsStart; i < analysisLen; ++i)
        {
            double v = static_cast<double>(analysisBuf[static_cast<size_t>(i)]);
            sumSq += v * v;
        }

        double rms = std::sqrt(sumSq / static_cast<double>( std::max(1, yinWindow) ));
        res.rmsLevel = static_cast<float>(rms);

        // Live vocal noise gate (0.003 = -50.5 dBFS)
        if (rms < 0.003)
        {
            return res;
        }

        yinBuf[0] = 1.0f;
        double runningSum = 0.0;

        for (int tau = 1; tau <= maxTau; ++tau)
        {
            float diffSum = 0.0f;
            for (int i = 0; i < yinWindow; ++i)
            {
                float d = analysisBuf[static_cast<size_t>(i)]
                        - analysisBuf[static_cast<size_t>(i + tau)];
                diffSum += d * d;
            }

            runningSum += static_cast<double>(diffSum);
            if (runningSum > 1e-12)
                yinBuf[static_cast<size_t>(tau)] = static_cast<float>(static_cast<double>(diffSum * static_cast<float>(tau)) / runningSum);
            else
                yinBuf[static_cast<size_t>(tau)] = 1.0f;
        }

        // Find first deep local minimum below 0.18 threshold
        const float threshold = 0.18f;
        int bestTau = -1;

        for (int tau = minTau; tau <= maxTau; ++tau)
        {
            if (yinBuf[static_cast<size_t>(tau)] < threshold)
            {
                while (tau + 1 <= maxTau && yinBuf[static_cast<size_t>(tau + 1)] < yinBuf[static_cast<size_t>(tau)])
                {
                    ++tau;
                }
                bestTau = tau;
                break;
            }
        }

        if (bestTau < 0)
        {
            return res;
        }

        // Parabolic interpolation around bestTau
        float refinedTau = static_cast<float>(bestTau);
        if (bestTau >= 2 && bestTau < maxTau)
        {
            float y0 = yinBuf[static_cast<size_t>(bestTau - 1)];
            float y1 = yinBuf[static_cast<size_t>(bestTau)];
            float y2 = yinBuf[static_cast<size_t>(bestTau + 1)];

            float denom = y0 - 2.0f * y1 + y2;
            if (std::abs(denom) > 1e-9f)
            {
                refinedTau += 0.5f * (y0 - y2) / denom;
            }
        }

        float freq = static_cast<float>(decimatedSr / static_cast<double>(refinedTau));
        if (freq > 60.0f && freq < 1100.0f)
        {
            res.frequencyHz = freq;
            res.confidence  = std::clamp(1.0f - yinBuf[static_cast<size_t>(bestTau)], 0.0f, 1.0f);
            res.isVoiced    = true;
        }

        return res;
    }

    double sr = 44100.0;
    double decimatedSr = 22050.0;
    int yinWindow = 485;
    int minTau = 22;
    int maxTau = 315;
    int analysisLen = 802;
    int hopSamples = 256;

    std::vector<float> decimRing;
    int ringMask = 16383;
    int decimWritePos = 0;
    int decimPairCount = 0;
    int hopCounter = 0;
    float decimAccum = 0.0f;

    std::vector<float> analysisBuf;
    std::vector<float> yinBuf;

    PitchDetectionResult lastResult;
};

} // namespace antitune
