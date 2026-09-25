# 125A Ugly Reverb - Manual

**Character Reverb / Controlled Wrongness**

125A Ugly Reverb is a dark, metallic character reverb for Windows x64. It is deliberately built around qualities that modern reverbs often try to suppress: audible modes, metallic ringing, hard reflections, unstable resonances and early-digital texture.

## Installation

1. Extract the ZIP archive completely.
2. Copy the complete `125A Ugly Reverb.vst3` folder to `C:\Program Files\Common Files\VST3\`.
3. Replace any older version if present.
4. Restart the DAW or run its VST3 plug-in rescan.

## Controls

- **Material** - Plate / Thin Plate / Heavy Plate / Sheet / Spring / Steel / Pipe / Metal Drum / Oil Can / Chamber / Tank.
- **Size** - scales the resonant structure and perceived space.
- **Decay** - controls tail persistence.
- **Pre-Delay** - 0 to 180 ms.
- **Diffusion** - controls how strongly parallel echoes fuse into the serial diffusion stages.
- **Damping** - high-frequency absorption inside the feedback network.
- **Body** - shifts the structural weighting of the resonant network.
- **Width** - controls processed stereo width.
- **Metal** - moves the network toward shorter, harder and more obviously metallic resonances.
- **Clang** - emphasizes selected modal paths and deliberately exposes ringing.
- **Rattle** - adds controlled mechanical modulation to selected resonances.
- **Digital Color** - Clean / 12-bit / 8-bit quantization inside the feedback network.
- **Mix** - linear dry/wet balance. 0% is dry only; 100% is pure wet with no dry contribution.
- **Output** - -12 dB to +12 dB.
- **Bypass** - exact plug-in bypass.

## Technical Characteristics

- VST3, Windows x64
- stereo input / stereo output
- 32-bit float processing
- 0 samples reported latency
- conservative 300-second reported reverb tail
- sample-accurate parameter automation
- versioned component state with migration from the earlier state format
- GUI zoom 100 / 125 / 150 / 175 / 200%
- 1x/2x HiDPI resources for knobs and wear/glass overlays

## DSP Concept

Metal, Clang and Rattle are part of the reverb network itself rather than simple post effects. The core combines material-specific comb and allpass structures, modal weighting, controlled feedback asymmetry, optional mechanical motion and optional digital quantization inside the feedback path.

## Practical Tips

- **Subtle:** keep Metal and Clang low, raise Diffusion, then tune Damping to taste.
- **Industrial:** push Metal and Clang, add Rattle, and try Pipe / Steel / Metal Drum materials.
- **Lo-fi:** set Digital Color to 12-bit or 8-bit. Quantization sits inside the feedback network, so it changes the tail itself.
- **Scale:** Size and Body reshape the resonant structure; extreme combinations are intentionally capable of strongly metallic results.

## V2 Validation Status

The exact V2.0.0 release candidate `cab06dabdfa815b9fab91c7f2b4c92369dcc41ce` passed the Windows x64 build, complete DSP measurement suite, Steinberg Validator (47/47), canonical plug-in package validation and five reload/editor lifecycle cycles. The release package uses the same validated VST3 bundle and includes 16 factory presets.

## License

The release is governed by the included 125A End User License Agreement. Music and audio created with the plug-in may be used commercially. The plug-in binary or release package may not be redistributed or resold without permission.

125A / AUDIO SOFTWARE
