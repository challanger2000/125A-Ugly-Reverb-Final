# 125A Ugly Reverb

**Character Reverb / Controlled Wrongness**

125A Ugly Reverb is a dark, metallic character reverb for Windows x64. It is deliberately designed around the qualities that modern reverbs often try to suppress: audible modes, metallic ringing, clang, hard reflections, unstable resonances and early-digital texture.

The goal is not random bad sound. The goal is **controlled ugliness**: repeatable character that can be pushed from usable plate/chamber ambience into intentionally harsh industrial spaces.

## Controls

### SPACE / MATERIAL

- **Material** — selects the underlying resonant network:
  Plate / Thin Plate / Heavy Plate / Sheet / Spring / Steel / Pipe / Metal Drum / Oil Can / Chamber / Tank.
- **Size** — scales the resonant structure and perceived space.
- **Decay** — controls the persistence of the tail.
- **Pre-Delay** — 0 to 180 ms.

### UGLY NETWORK

- **Diffusion** — controls how strongly the parallel echoes fuse into the serial diffusion stages.
- **Damping** — high-frequency absorption inside the feedback network.
- **Body** — changes the low/mid structural balance of the resonant paths.
- **Width** — controls processed stereo width.
- **Metal** — shifts the network toward shorter, harder and more obviously metallic paths.
- **Clang** — emphasizes selected modal paths and deliberately exposes ringing.
- **Rattle** — adds controlled mechanical-style modulation to selected resonances.
- **Digital Color** — Clean / 12-bit / 8-bit quantization inside the feedback network.

### MASTER

- **Mix** — linear dry/wet balance.
- **Output** — -12 dB to +12 dB.
- **Bypass** — exact plug-in bypass.

## GUI

The editor supports:

- 100%
- 125%
- 150%
- 175%
- 200%

The selected zoom level is stored in the controller state. Knobs and the wear/glass texture layers use multi-resolution 1x/2x resources.

## Technical characteristics

- VST3
- Windows x64
- Stereo input / stereo output
- 32-bit float audio processing
- 0 samples reported processing latency
- finite 150-second reported reverb tail
- sample-accurate parameter automation
- versioned component state with migration from the earlier unversioned state format
- realtime-safe processing restart reset
- canonical VST3 bundle packaging

## DSP concept

Metal, Clang and Rattle are part of the reverb network itself rather than simple post-processing effects.

The core combines material-specific comb and allpass structures, modal weighting, controlled feedback asymmetry, optional mechanical motion and optional in-loop digital quantization. At low character settings the result can still behave like a usable plate/chamber reverb; at high settings it intentionally becomes metallic, coarse and unstable.

## Final validation

The approved Windows final candidate was frozen from GitHub Actions build **#67** in September 2026.

Validation of that stand:

- GitHub Actions build: **SUCCESS**
- DSP measurement suite: **PASS**
- Steinberg VST3 Validator: **47 tests passed / 0 failed**
- canonical packaged VST3 validation: **PASS**
- reload/lifecycle diagnostic: **5/5 cycles PASS**
- 125A Plugin Tester: **PASS**
- project-owned compiler warnings: **0**

The final repository imported the approved code/resource tree byte-for-byte from the tested Development freeze before release-only documentation was added.

## Installation

Copy the complete `125A Ugly Reverb.vst3` bundle to:

`C:\Program Files\Common Files\VST3\`

Then restart the DAW or run its VST3 rescan if required.

## Project

125A / AUDIO SOFTWARE
