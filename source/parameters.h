#pragma once
#include "pluginterfaces/vst/vsttypes.h"

namespace UglyReverb {

enum ParamIds : Steinberg::Vst::ParamID
{
    kMaterial = 100,
    kSize,
    kDecay,
    kPreDelay,
    kDiffusion,
    kDamping,
    kMetal,
    kClang,
    kRattle,
    kBody,
    kWidth,
    kMix,
    kOutput,
    kDigital,
    kBypass
};

} // namespace UglyReverb
