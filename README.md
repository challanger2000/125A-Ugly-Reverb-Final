# 125A Ugly Reverb

A dark, metallic character reverb focused on clang, resonance, industrial spaces and early-digital character.

## Design goal

Ugly Reverb intentionally cultivates qualities that modern reverbs often try to suppress: audible modes, metallic ringing, clang, hard reflections and artificial spatial structure.

The target is not "bad reverb". The target is **controlled ugliness**: useful, repeatable and musical character for guitars, snares, synths, vocals, sound design and industrial/dark productions.

## Current feature set

- Stereo VST3 for Windows x64
- 32-bit float processing
- 0 samples reported latency
- finite reported reverb tail
- Material: Plate / Thin Plate / Heavy Plate / Sheet / Spring / Steel / Pipe / Metal Drum / Oil Can / Chamber / Tank
- Size
- Decay
- Pre-Delay
- Diffusion
- Damping
- Metal
- Clang
- Rattle
- Body
- Width
- Mix
- Output
- Digital Color: Clean / 12-bit / 8-bit
- Bypass
- editable numeric value fields
- GUI zoom: 100 / 125 / 150 / 175 / 200%
- controller-private GUI zoom persistence
- hybrid VSTGUI interface with procedural faceplate, transparent wear/glass layers and multi-resolution animated knob strips

## DSP principle

Metal, Clang and Rattle are not merely post-effects after a conventional reverb. They alter the feedback/diffusion behaviour of the reverb network itself.

The reverb core uses intentionally non-modern comb/allpass structures, material-specific modal fingerprints and controlled instability. Digital Color applies optional clean / 12-bit / 8-bit quantization inside the feedback network.

## Material modes

- **Plate** — dense, early-digital metallic sheet
- **Thin Plate** — brighter, faster and lighter sheet character
- **Heavy Plate** — denser and weightier plate body
- **Sheet** — thin, direct, deliberately rattly metal
- **Spring** — mechanical, resonant and lightly moving spring character
- **Steel** — harder, more exposed modal structure
- **Pipe** — narrow, tubular and strongly modal
- **Metal Drum** — hollow metal shell / barrel-like body
- **Oil Can** — gently unstable electromechanical-style character
- **Chamber** — cold industrial resonant chamber
- **Tank** — larger resonant metallic body

## Verification

The development workflow builds the canonical VST3 bundle, runs DSP measurement tests, Steinberg Validator and a five-cycle reload/lifecycle probe before uploading the Windows artifact.

This repository is the development repository. The approved release stand is copied separately into the Final repository only after GUI approval and final external tester PASS.
