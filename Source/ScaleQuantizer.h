#pragma once

#include <cmath>
#include <array>
#include <string>

namespace antitune
{

enum class ScaleType : int
{
    NaturalMinor = 0,
    Major,
    PentatonicMinor,
    HarmonicMinor,
    TrapPhrygian,
    PentatonicMajor,
    Dorian,
    Mixolydian,
    Blues,
    Chromatic
};

class ScaleQuantizer
{
public:
    static constexpr const char* kNoteNames[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    static std::array<bool, 12> getScaleMask(ScaleType scale, int rootKey)
    {
        // Bitmasks matching VocalSnap's musical scale definitions
        uint16_t bitmask = 0xfff;
        switch (scale)
        {
            case ScaleType::NaturalMinor:    bitmask = 0x5ad; break; // 1 0 1 1 0 1 0 1 1 0 1 0
            case ScaleType::Major:           bitmask = 0xab5; break; // 1 0 1 0 1 1 0 1 0 1 0 1
            case ScaleType::PentatonicMinor: bitmask = 0x4a9; break; // 1 0 0 1 0 1 0 1 0 0 1 0
            case ScaleType::HarmonicMinor:   bitmask = 0x9ad; break; // 1 0 1 1 0 1 0 1 1 0 0 1
            case ScaleType::TrapPhrygian:    bitmask = 0x5b3; break; // 1 1 0 0 1 1 0 1 1 0 1 0
            case ScaleType::PentatonicMajor: bitmask = 0x295; break; // 1 0 1 0 1 0 0 1 0 1 0 0
            case ScaleType::Dorian:          bitmask = 0x6ad; break;
            case ScaleType::Mixolydian:      bitmask = 0x6b5; break;
            case ScaleType::Blues:           bitmask = 0x4e9; break;
            case ScaleType::Chromatic:
            default:                         bitmask = 0xfff; break;
        }

        std::array<bool, 12> mask {};
        int safeRoot = ((rootKey % 12) + 12) % 12;
        for (int pc = 0; pc < 12; ++pc)
        {
            int rel = ((pc - safeRoot) % 12 + 12) % 12;
            mask[static_cast<size_t>(pc)] = ((bitmask >> rel) & 1) != 0;
        }
        return mask;
    }

    static float freqToMidi(float freqHz)
    {
        if (freqHz <= 20.0f)
            return 0.0f;
        return 69.0f + 12.0f * std::log2(freqHz / 440.0f);
    }

    static float midiToFreq(float midiNote)
    {
        return 440.0f * std::pow(2.0f, (midiNote - 69.0f) / 12.0f);
    }

    // Exact VocalSnap hysteresis quantizer:
    // If lastTargetMidi is valid and active in the scale, and the singer's voice is within
    // 0.62 semitones (62 cents) of lastTargetMidi, it stays locked to lastTargetMidi!
    // This completely stops the pitch from violently pulling left/right between adjacent notes.
    static int quantizeMidiNote(float continuousMidi,
                                const std::array<bool, 12>& activeNotes,
                                int lastTargetMidi)
    {
        if (lastTargetMidi >= 0)
        {
            int lastPc = ((lastTargetMidi % 12) + 12) % 12;
            if (activeNotes[static_cast<size_t>(lastPc)])
            {
                if (std::abs(continuousMidi - static_cast<float>(lastTargetMidi)) < 0.62f)
                {
                    return lastTargetMidi;
                }
            }
        }

        int centerMidi = static_cast<int>(std::llround(continuousMidi));
        int bestNote = centerMidi;
        float bestDist = 1e9f;

        for (int candidate = centerMidi - 7; candidate <= centerMidi + 7; ++candidate)
        {
            int pc = ((candidate % 12) + 12) % 12;
            if (activeNotes[static_cast<size_t>(pc)])
            {
                float dist = std::abs(continuousMidi - static_cast<float>(candidate));
                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestNote = candidate;
                }
            }
        }

        return bestNote;
    }

    static std::string midiNoteToString(int midiNote)
    {
        if (midiNote <= 0 || midiNote > 127)
            return "--";
        int pc = ((midiNote % 12) + 12) % 12;
        int octave = (midiNote / 12) - 1;
        return std::string(kNoteNames[pc]) + std::to_string(octave);
    }
};

} // namespace antitune
