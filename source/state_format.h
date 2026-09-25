#pragma once

#include "base/source/fstreamer.h"
#include <cstring>
#include <cmath>

namespace UglyReverb {

constexpr Steinberg::int32 kComponentStateMagic = 0x7FC14755; // quiet-NaN bit pattern: cannot collide with a valid normalized legacy Material value
constexpr Steinberg::int32 kComponentStateVersion = 1;
constexpr int kComponentStateValueCount = 14;

inline bool readComponentStatePayload(Steinberg::IBStreamer& stream,
                                      float (&values)[kComponentStateValueCount],
                                      Steinberg::int32& bypass)
{
    Steinberg::int32 marker = 0;
    if (!stream.readInt32(marker))
        return false;

    if (marker == kComponentStateMagic)
    {
        Steinberg::int32 version = 0;
        if (!stream.readInt32(version) || version != kComponentStateVersion)
            return false;

        for (float& value : values)
            if (!stream.readFloat(value))
                return false;
    }
    else
    {
        static_assert(sizeof(float) == sizeof(Steinberg::int32),
                      "Legacy state migration requires 32-bit float/int32");
        std::memcpy(&values[0], &marker, sizeof(float));
        for (int i = 1; i < kComponentStateValueCount; ++i)
            if (!stream.readFloat(values[i]))
                return false;
    }

    if (!stream.readInt32(bypass))
        return false;

    for (float value : values)
        if (!std::isfinite(value) || value < 0.f || value > 1.f)
            return false;

    bypass = bypass ? 1 : 0;
    return true;
}

inline bool writeComponentStatePayload(Steinberg::IBStreamer& stream,
                                       const float (&values)[kComponentStateValueCount],
                                       Steinberg::int32 bypass)
{
    if (!stream.writeInt32(kComponentStateMagic))
        return false;
    if (!stream.writeInt32(kComponentStateVersion))
        return false;
    for (float value : values)
        if (!stream.writeFloat(value))
            return false;
    return stream.writeInt32(bypass);
}

} // namespace UglyReverb
