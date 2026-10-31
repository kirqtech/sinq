# sinq — Live T-Pain 0ms Pitch Correction & 20-Band Studio Vocoder

**sinq** is a real-time C++ / JUCE vocal processor built as a **Standalone macOS Application (`.app`)**, **VST3 Plugin (`.vst3`)**, and **Audio Unit Plugin (`.component`)**.

It combines a low-latency, phase-coupled pitch correction engine (modeled for zero-chop live vocal performance and instant `0 ms` T-Pain hard-snap steps) with an independent, switchable **20-Band Harmonic Studio Vocoder** and **Voice Pitch Shifter (`-12` to `+12` semitones)**.

---

## 📁 Project Structure

The repository layout looks like this:

```text
sinq/
├── CMakeLists.txt              # CMake build configuration (AU, VST3, Standalone + JUCE FetchContent)
├── README.md                   # Build & installation documentation
└── Source/
    ├── PitchDetector.h         # 2x-decimated YIN real-time pitch detector
    ├── ScaleQuantizer.h        # Musical scales, custom 12-key mask & 0.62-semitone hysteresis
    ├── LaserPitchShifter.h     # Phase-coupled Dual-Hermite pitch shifter & 20-Band Studio Vocoder
    ├── PluginProcessor.h       # AudioProcessor state, parameters (APVTS) & telemetry
    ├── PluginProcessor.cpp     # Real-time audio block processing & parameter smoothing
    ├── PluginEditor.h          # Custom JUCE LookAndFeel & studio GUI header
    └── PluginEditor.cpp        # Real-time pitch ring, pitch history graph & interactive keyboard
```

---

## 🛠️ Prerequisites (macOS)

Before building the C++ source files into a runnable `.app`, `.vst3`, and `.component`, ensure you have the following installed on macOS:

1. **Xcode Command Line Tools** (provides `clang++`, macOS SDK, `codesign`, and `auval`):
   ```bash
   xcode-select --install
   ```
2. **CMake (version 3.22 or newer)** and **Git**:
   If you use [Homebrew](https://brew.sh/), install them via:
   ```bash
   brew install cmake git
   ```

---

## 🚀 Step-by-Step Build Instructions

Follow these steps from a terminal inside the project root directory (`/path/to/sinq`):

### Step 1: Configure the CMake Build System
Generate the `Release` build directory (`./build`). `CMakeLists.txt` uses CMake's `FetchContent` to automatically download **JUCE 8.0.4**, compile `juceaide`, and generate the build files:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

### Step 2: Compile All Targets (Standalone, VST3, and AU)
Compile the project using all available CPU cores (`-j 8`):

```bash
cmake --build build --config Release -j 8
```

> **💡 Tip on Incremental Builds (Build Cache):**
> Once the `build/` directory is created and compiled the first time, all compiled JUCE `.o` object files are cached inside `build/CMakeFiles/`. When you edit any file in `Source/`, simply re-run `cmake --build build --config Release -j 8` — it will recompile **only** the modified source files in ~2 seconds.

---

## 📦 Where to Find the Built Files

After the build reaches `100%`, your compiled binaries are located inside `build/sinq_artefacts/Release/`:

| Format | Built Artifact Path | Target Use |
| :--- | :--- | :--- |
| **Standalone App** | `build/sinq_artefacts/Release/Standalone/sinq.app` | Run directly on macOS with Mic & Headphones (no DAW needed) |
| **Audio Unit (AU)** | `build/sinq_artefacts/Release/AU/sinq.component` | GarageBand, Logic Pro, Ableton Live (macOS) |
| **VST3 Plugin** | `build/sinq_artefacts/Release/VST3/sinq.vst3` | Ableton Live, FL Studio, Reaper, Cubase |

---

## 🔏 Code-Signing & Installing the Plugins on macOS

On Apple Silicon (`arm64`) Macs, macOS requires plugins and standalone bundles to have a valid ad-hoc signature before DAWs will load them.

### 1. Ad-Hoc Code-Sign the Built Bundles
```bash
codesign --force --deep --sign - build/sinq_artefacts/Release/AU/sinq.component
codesign --force --deep --sign - build/sinq_artefacts/Release/VST3/sinq.vst3
codesign --force --deep --sign - build/sinq_artefacts/Release/Standalone/sinq.app
```

### 2. Copy Standalone App to Project Root (Optional Convenience)
```bash
cp -R build/sinq_artefacts/Release/Standalone/sinq.app ./sinq.app
```

### 3. Install AU and VST3 into Your macOS Audio Plug-Ins Folders
```bash
mkdir -p ~/Library/Audio/Plug-Ins/Components
mkdir -p ~/Library/Audio/Plug-Ins/VST3

cp -R build/sinq_artefacts/Release/AU/sinq.component ~/Library/Audio/Plug-Ins/Components/
cp -R build/sinq_artefacts/Release/VST3/sinq.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

### 4. Verify Audio Unit Registration (Optional)
You can run Apple's official Audio Unit validation tool to confirm GarageBand and Logic Pro recognize the plugin:
```bash
auval -v aufx Sq01 Sinq
```
You should see `AU VALIDATION SUCCEEDED.` at the bottom of the output.

---

## 🎤 How to Use sinq

### Using the Standalone App (Mic -> Headphones)
1. Double-click **`sinq.app`**.
2. Click **Options -> Audio/MIDI Settings...** in the top-left corner of the window.
3. Select your **Input** (Microphone / Audio Interface) and **Output** (Wired Headphones).
4. Set **Audio buffer size** to **`64`** or **`128` samples** (~1.5–2.5 ms latency) for live monitoring.
5. Or click **`TEST VOICE`** in the top-right corner of the plugin to hear the built-in vocal glissando generator without singing into a mic.

### Controls Overview
* **Root Key & Scale**: Choose your song's root note (`C` – `B`) and scale (`Natural Minor`, `Major`, `Pentatonic Minor (T-Pain / Trap)`, `Harmonic Minor`, `Trap Phrygian`, `Dorian`, `Mixolydian`, `Blues`, `Chromatic`). Click any of the 12 note buttons on the keyboard strip to manually toggle individual notes on/off.
* **`T-PAIN 0ms SNAP`**: Locks retune speed to an ultra-fast `1.0 ms` hard-snap response for classic step-quantized T-Pain / Travis Scott vocals without clicks.
* **`RETUNE SPEED` (`0 – 200 ms`)**: Controls how fast your voice is pulled to the target note when `T-PAIN 0ms SNAP` is turned off.
* **`CORRECTION` (`0% – 100%`)**: Controls pitch-correction strength (`100%` = full lock to scale note center; `50%–80%` = gentle pull without hard-snapping all the way).
* **`PITCH SHIFT` (`-12.0` to `+12.0 st`)**: Raises or lowers your overall voice pitch by up to an octave while keeping scale quantization active.
* **`VOCODER ON` + `VOCODER MIX` & `VOCODER TONE`**: Enables the separate 20-Band Harmonic Studio Vocoder synced to your Auto-Tuned pitch (`0% Tone` = warm talkbox/pad, `100% Tone` = bright synth lead).
