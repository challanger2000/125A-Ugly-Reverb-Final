#include "processor.h"
#include "controller.h"
#include "ids.h"
#include "parameters.h"
#include "state_format.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace UglyReverb {

namespace {
constexpr float kPi = 3.14159265358979323846f;

inline float clamp1(float x)
{
    return std::max(-1.f, std::min(1.f, x));
}

inline float zapTiny(float x)
{
    // Far below any audible level; prevents long feedback tails from entering
    // subnormal arithmetic without adding dither/noise to silence.
    return std::fabs(x) < 1.0e-20f ? 0.f : x;
}

inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

inline void hadamard8(const std::array<float, 8>& in, std::array<float, 8>& out)
{
    out = in;
    for (int stride = 1; stride < 8; stride <<= 1)
    {
        for (int base = 0; base < 8; base += stride << 1)
        {
            for (int j = 0; j < stride; ++j)
            {
                const float a = out[(size_t)(base + j)];
                const float b = out[(size_t)(base + j + stride)];
                out[(size_t)(base + j)] = a + b;
                out[(size_t)(base + j + stride)] = a - b;
            }
        }
    }

    constexpr float kNorm = 0.35355339059327376220f; // 1 / sqrt(8)
    for (float& v : out)
        v *= kNorm;
}
}

void Processor::DelayLine::resize(int samples)
{
    data.assign(std::max(samples, 8), 0.f);
    write = 0;
    lp = 0.f;
}

void Processor::DelayLine::clear()
{
    std::fill(data.begin(), data.end(), 0.f);
    write = 0;
    lp = 0.f;
}

float Processor::DelayLine::read(float delaySamples) const
{
    if (data.empty()) return 0.f;
    const float d = std::max(1.f, std::min(delaySamples, (float)data.size() - 2.f));
    const int d0 = (int)std::floor(d);
    const float frac = d - (float)d0;

    int i0 = write - d0;
    while (i0 < 0) i0 += (int)data.size();
    int i1 = i0 - 1;
    if (i1 < 0) i1 += (int)data.size();

    return data[(size_t)i0] * (1.f - frac) + data[(size_t)i1] * frac;
}

void Processor::DelayLine::push(float x)
{
    if (data.empty()) return;
    data[(size_t)write] = zapTiny(x);
    if (++write >= (int)data.size()) write = 0;
}

Processor::Processor()
{
    setControllerClass(ControllerUID);
}

tresult PLUGIN_API Processor::initialize(FUnknown* context)
{
    auto r = AudioEffect::initialize(context);
    if (r != kResultOk) return r;
    addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    return kResultOk;
}

tresult PLUGIN_API Processor::terminate()
{
    return AudioEffect::terminate();
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup)
{
    sampleRate_ = setup.sampleRate > 1.0 ? setup.sampleRate : 44100.0;
    resetDsp();
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API Processor::setActive(TBool state)
{
    if (state) resetDsp();
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API Processor::setProcessing(TBool state)
{
    // Hosts may restart processing without another setActive() transition.
    // Clear existing buffers without reallocating: setProcessing() may be called
    // from the realtime thread.
    if (state)
        clearDsp();

    AudioEffect::setProcessing(state);
    return kResultTrue;
}

uint32 PLUGIN_API Processor::getTailSamples()
{
    // At extreme Size/Body settings the longest active tank path is about
    // 0.32 s. With the 0.991 feedback ceiling, a conservative -60 dB bound is
    // roughly 244 s before accounting for small modulation/offsets. Report
    // 300 s so hosts do not truncate legitimate V2 tails at extreme settings.
    constexpr double kReportedTailSeconds = 300.0;
    const double samples = std::ceil(sampleRate_ * kReportedTailSeconds);
    return static_cast<uint32>(std::min<double>(
        samples, static_cast<double>(std::numeric_limits<uint32>::max())));
}

tresult PLUGIN_API Processor::canProcessSampleSize(int32 symbolicSampleSize)
{
    return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                                  SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns == 1 && numOuts == 1 &&
        inputs[0] == SpeakerArr::kStereo && outputs[0] == SpeakerArr::kStereo)
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    return kResultFalse;
}

void Processor::resetDsp()
{
    // Tank at maximum Size/Body can exceed 180 ms. Keep enough headroom so
    // material tuning is never silently clamped by the delay-line capacity.
    const int maxComb = (int)(sampleRate_ * 0.35) + 32;
    const int maxAp = (int)(sampleRate_ * 0.045) + 32;
    for (auto& x : combL_) x.resize(maxComb);
    for (auto& x : combR_) x.resize(maxComb);
    for (auto& x : apL_) x.resize(maxAp);
    for (auto& x : apR_) x.resize(maxAp);

    const int maxPre = (int)(sampleRate_ * 0.25) + 16;
    preL_.assign(maxPre, 0.f);
    preR_.assign(maxPre, 0.f);

    clearDsp();
}

void Processor::clearDsp()
{
    for (auto& x : combL_) x.clear();
    for (auto& x : combR_) x.clear();
    for (auto& x : apL_) x.clear();
    for (auto& x : apR_) x.clear();
    std::fill(preL_.begin(), preL_.end(), 0.f);
    std::fill(preR_.begin(), preR_.end(), 0.f);
    preWrite_ = 0;

    for (int i = 0; i < kCombs; ++i)
        rattlePhase_[i] = (2.f * kPi * (float)i) / (float)kCombs;

    resetSmoothers();
}

void Processor::resetSmoothers()
{
    smSize_ = size_;
    smDecay_ = decay_;
    smPreDelay_ = preDelay_;
    smDiffusion_ = diffusion_;
    smDamping_ = damping_;
    smMetal_ = metal_;
    smClang_ = clang_;
    smRattle_ = rattle_;
    smBody_ = body_;
    smWidth_ = width_;
    smMix_ = mix_;
    smOutput_ = output_;
}

float Processor::processDigital(float x) const
{
    const int mode = std::max(0, std::min(2, (int)std::lround(digital_ * 2.f)));
    if (mode == 0) return x;
    const float scale = mode == 1 ? 2047.f : 127.f;
    return std::round(clamp1(x) * scale) / scale;
}

float Processor::processAllpass(DelayLine& line, float input, float delaySamples, float feedback)
{
    const float delayed = line.read(delaySamples);
    const float y = delayed - input;
    line.push(input + delayed * feedback);
    return y;
}

void Processor::applyParameter(ParamID id, float value)
{
    const float f = std::max(0.f, std::min(1.f, value));
    switch (id)
    {
        case kMaterial: material_ = f; break;
        case kSize: size_ = f; break;
        case kDecay: decay_ = f; break;
        case kPreDelay: preDelay_ = f; break;
        case kDiffusion: diffusion_ = f; break;
        case kDamping: damping_ = f; break;
        case kMetal: metal_ = f; break;
        case kClang: clang_ = f; break;
        case kRattle: rattle_ = f; break;
        case kBody: body_ = f; break;
        case kWidth: width_ = f; break;
        case kMix: mix_ = f; break;
        case kOutput: output_ = f; break;
        case kDigital: digital_ = f; break;
        case kBypass: bypass_ = f > 0.5f; break;
        default: break;
    }
}

tresult PLUGIN_API Processor::process(ProcessData& data)
{
    // Cache host automation queues without allocating in the audio thread.
    // Targets are applied at their VST3 sample offsets; continuous controls then
    // keep using the existing short smoothing time.
    static constexpr int32 kMaxParamQueues = 16;
    IParamValueQueue* paramQueues[kMaxParamQueues] {};
    int32 pointIndex[kMaxParamQueues] {};
    int32 pointCount[kMaxParamQueues] {};
    int32 nextOffset[kMaxParamQueues] {};
    ParamValue nextValue[kMaxParamQueues] {};
    int32 activeQueues = 0;

    if (data.inputParameterChanges)
    {
        const int32 count = std::min<int32>(
            data.inputParameterChanges->getParameterCount(), kMaxParamQueues);
        for (int32 i = 0; i < count; ++i)
        {
            auto* q = data.inputParameterChanges->getParameterData(i);
            if (!q || q->getPointCount() <= 0) continue;

            const int32 slot = activeQueues++;
            paramQueues[slot] = q;
            pointCount[slot] = q->getPointCount();
            pointIndex[slot] = 0;
            if (q->getPoint(0, nextOffset[slot], nextValue[slot]) != kResultTrue)
            {
                --activeQueues;
                continue;
            }
        }
    }

    const auto consumeRemainingParameterPoints = [&]()
    {
        for (int32 qIndex = 0; qIndex < activeQueues; ++qIndex)
        {
            while (pointIndex[qIndex] < pointCount[qIndex])
            {
                applyParameter(paramQueues[qIndex]->getParameterId(),
                               (float)nextValue[qIndex]);
                ++pointIndex[qIndex];
                if (pointIndex[qIndex] < pointCount[qIndex] &&
                    paramQueues[qIndex]->getPoint(pointIndex[qIndex],
                                                  nextOffset[qIndex],
                                                  nextValue[qIndex]) != kResultTrue)
                {
                    pointIndex[qIndex] = pointCount[qIndex];
                }
            }
        }
    };

    // Parameter-only process calls are valid VST3 host behaviour.  Even with
    // zero audio samples, consume queued values so processor/controller state
    // remains synchronized.
    if (data.numSamples <= 0)
    {
        consumeRemainingParameterPoints();
        return kResultOk;
    }

    if (data.numInputs < 1 || data.numOutputs < 1)
    {
        // Parameter-only positive-length blocks still need to update processor state.
        consumeRemainingParameterPoints();
        return kResultOk;
    }
    if (data.symbolicSampleSize != kSample32)
        return kResultFalse;

    // Defensive host-data validation before indexing the bus arrays.  A valid
    // VST3 host supplies these when numInputs/numOutputs are positive, but a
    // malformed call must fail cleanly instead of becoming a null dereference.
    if (!data.inputs || !data.outputs)
        return kResultFalse;

    auto** in = data.inputs[0].channelBuffers32;
    auto** out = data.outputs[0].channelBuffers32;
    if (!in || !out || data.inputs[0].numChannels < 2 || data.outputs[0].numChannels < 2)
        return kResultFalse;

    const float smoothCoef = 1.f - std::exp(-1.f / std::max(1.f, 0.012f * (float)sampleRate_));

    // Eleven deliberately non-modern material networks.  Normalized legacy
    // anchors remain stable: 0.0 = Plate, 0.5 = Steel, 1.0 = Tank.
    // Plate / Thin Plate / Heavy Plate / Sheet / Spring / Steel /
    // Pipe / Metal Drum / Oil Can / Chamber / Tank.
    static constexpr int kMaterials = 11;
    static constexpr float baseMs[kMaterials][kCombs] = {
        {23.1f,27.8f,31.7f,36.4f,41.9f,47.3f,53.6f,61.2f},   // Plate
        {14.8f,18.1f,21.7f,26.4f,31.2f,37.9f,45.6f,55.1f},   // Thin Plate
        {28.4f,33.9f,39.6f,46.8f,55.3f,65.1f,76.7f,90.4f},   // Heavy Plate
        {10.9f,13.7f,17.2f,21.8f,27.6f,35.1f,44.7f,57.2f},   // Sheet
        {12.4f,15.9f,20.8f,27.5f,36.2f,47.6f,62.8f,83.1f},   // Spring
        {17.8f,22.6f,28.3f,34.7f,42.1f,51.8f,63.4f,78.6f},   // Steel
        {18.6f,24.8f,33.2f,44.7f,60.1f,80.9f,108.7f,145.3f}, // Pipe
        {24.7f,31.8f,40.9f,52.6f,67.8f,86.7f,109.8f,137.4f}, // Metal Drum
        {16.1f,21.4f,28.9f,39.2f,53.1f,71.6f,96.4f,129.8f},  // Oil Can
        {20.6f,26.9f,34.1f,43.8f,55.7f,69.4f,86.2f,107.5f},  // Chamber
        {31.6f,39.3f,48.7f,59.8f,73.1f,88.4f,104.7f,126.3f}  // Tank
    };
    static constexpr float uglyMs[kMaterials][kCombs] = {
        {7.3f,9.8f,12.7f,16.9f,22.4f,29.1f,37.8f,49.6f},
        {4.2f,5.9f,8.1f,11.2f,15.6f,21.8f,30.4f,42.7f},
        {8.6f,11.9f,15.8f,21.3f,28.7f,38.5f,51.4f,68.2f},
        {3.7f,5.1f,7.3f,10.4f,14.9f,21.5f,31.2f,45.8f},
        {4.8f,6.9f,9.7f,13.6f,19.4f,28.1f,40.7f,59.2f},
        {5.9f,8.4f,11.6f,15.7f,21.3f,28.9f,39.4f,54.1f},
        {5.4f,8.0f,12.1f,18.2f,27.3f,41.0f,61.7f,92.6f},
        {7.2f,10.4f,15.0f,21.7f,31.4f,45.6f,66.2f,96.1f},
        {5.1f,7.6f,11.4f,17.0f,25.5f,38.3f,57.6f,86.4f},
        {6.8f,9.6f,13.7f,19.4f,27.2f,38.1f,53.6f,74.9f},
        {9.1f,12.8f,17.6f,24.3f,33.7f,46.2f,63.9f,86.7f}
    };
    static constexpr float apMs[kMaterials][kAllpasses] = {
        {4.7f,7.1f,10.9f,15.8f},
        {2.9f,4.6f,7.4f,11.2f},
        {5.3f,8.0f,12.1f,17.3f},
        {2.4f,3.9f,6.1f,9.4f},
        {3.1f,5.3f,8.8f,14.6f},
        {3.8f,6.2f,9.6f,14.1f},
        {3.5f,5.8f,9.9f,17.1f},
        {4.6f,7.7f,12.7f,19.3f},
        {3.7f,6.4f,10.6f,17.8f},
        {4.2f,6.9f,10.8f,16.4f},
        {5.9f,8.7f,13.2f,18.6f}
    };

    // Distinct modal fingerprints; these are intentionally not just EQ/gain presets.
    static constexpr float clangShape[kMaterials][kCombs] = {
        {0.42f,0.74f,0.18f,0.66f,0.82f,0.28f,0.71f,0.12f},
        {0.68f,0.92f,0.31f,0.83f,1.00f,0.42f,0.88f,0.22f},
        {0.35f,0.79f,0.11f,0.72f,0.86f,0.24f,0.76f,0.08f},
        {0.76f,1.00f,0.52f,0.91f,0.84f,0.63f,0.95f,0.38f},
        {0.31f,1.00f,0.08f,0.88f,0.44f,0.94f,0.19f,0.79f},
        {0.08f,1.00f,-0.16f,0.72f,1.00f,0.03f,0.91f,-0.12f},
        {1.00f,0.18f,0.82f,-0.06f,0.91f,0.12f,0.74f,-0.10f},
        {0.24f,0.93f,0.06f,1.00f,0.41f,0.84f,0.68f,0.29f},
        {0.16f,0.86f,-0.04f,0.69f,1.00f,0.21f,0.78f,0.05f},
        {0.18f,0.87f,-0.05f,0.94f,0.49f,0.21f,1.00f,0.02f},
        {0.22f,0.81f,-0.08f,1.00f,0.58f,0.18f,0.97f,0.05f}
    };
    static constexpr float combWeight[kMaterials][kCombs] = {
        {0.92f,0.88f,0.96f,0.90f,0.94f,0.89f,0.93f,0.91f},
        {0.96f,1.02f,0.90f,1.05f,0.93f,1.01f,0.89f,0.98f},
        {0.88f,0.95f,1.02f,0.91f,1.00f,0.94f,0.97f,0.90f},
        {1.05f,0.91f,1.08f,0.87f,1.02f,0.89f,1.04f,0.85f},
        {0.72f,1.16f,0.69f,1.08f,0.76f,1.12f,0.71f,1.03f},
        {0.62f,1.16f,0.58f,0.96f,1.22f,0.66f,1.08f,0.61f},
        {1.18f,0.61f,1.09f,0.56f,1.15f,0.63f,1.02f,0.59f},
        {0.74f,1.08f,0.82f,1.14f,0.71f,1.01f,0.94f,0.79f},
        {0.81f,1.07f,0.70f,0.98f,1.13f,0.76f,1.02f,0.73f},
        {0.70f,1.05f,0.72f,1.11f,0.78f,0.97f,1.14f,0.69f},
        {0.78f,1.02f,0.70f,1.12f,0.76f,0.94f,1.18f,0.72f}
    };
    static constexpr float rt60Scale[kMaterials]     = {0.92f,0.80f,1.08f,0.76f,0.96f,1.00f,1.05f,1.12f,1.02f,1.10f,1.18f};
    static constexpr float diffusionBias[kMaterials] = {0.10f,0.14f,0.08f,0.16f,0.06f,-0.05f,-0.08f,-0.02f,0.03f,0.00f,-0.10f};
    static constexpr float rawLeakScale[kMaterials]  = {0.72f,0.82f,0.68f,0.90f,0.96f,1.18f,1.20f,1.08f,1.02f,0.96f,1.06f};
    static constexpr float materialGain[kMaterials]  = {1.00f,1.02f,1.04f,1.05f,1.06f,1.14f,1.10f,1.08f,1.06f,1.08f,1.08f};
    static constexpr float clangDepth[kMaterials]    = {0.050f,0.060f,0.052f,0.064f,0.070f,0.074f,0.078f,0.068f,0.066f,0.064f,0.066f};
    static constexpr float intrinsicMotion[kMaterials]= {0.f,0.f,0.f,0.00010f,0.00075f,0.f,0.00008f,0.00012f,0.00110f,0.00010f,0.f};
    static constexpr float motionRate[kMaterials]    = {1.f,1.f,1.f,1.7f,3.4f,1.f,1.3f,1.1f,0.55f,0.8f,1.f};
    // Relative high-frequency survival inside the feedback network.
    // Higher values retain a harder/brighter metallic tail; lower values make
    // the material lose top end faster without changing the user-facing Decay.
    static constexpr float hfMaterial[kMaterials] = {
        0.72f,0.84f,0.62f,0.91f,0.88f,0.94f,0.96f,0.89f,0.82f,0.70f,0.64f
    };

    // Deterministic sparse early-reflection fingerprints.  These are not a
    // room simulator; they are intentionally irregular material exciters that
    // increase onset density before the FDN tank while preserving repeatability.
    static constexpr float earlyMs[kMaterials][6] = {
        { 3.1f, 5.7f, 8.9f,12.8f,17.6f,23.4f}, // Plate
        { 2.2f, 4.1f, 6.8f,10.1f,14.7f,20.9f}, // Thin Plate
        { 4.0f, 7.3f,11.2f,16.0f,22.1f,29.8f}, // Heavy Plate
        { 1.8f, 3.6f, 5.9f, 8.7f,12.9f,18.8f}, // Sheet
        { 2.5f, 4.8f, 7.7f,11.5f,16.3f,22.7f}, // Spring
        { 2.9f, 5.2f, 8.4f,12.1f,16.9f,23.8f}, // Steel
        { 3.6f, 6.4f,10.5f,15.7f,22.8f,31.6f}, // Pipe
        { 4.4f, 7.9f,12.7f,18.8f,26.4f,35.7f}, // Metal Drum
        { 3.0f, 5.6f, 9.1f,13.8f,20.0f,28.2f}, // Oil Can
        { 4.8f, 8.6f,13.4f,19.7f,27.0f,36.1f}, // Chamber
        { 5.5f, 9.7f,14.9f,21.6f,29.4f,39.8f}  // Tank
    };
    static constexpr float earlySign[kMaterials][6] = {
        { 1.f,-1.f, 1.f, 1.f,-1.f, 1.f},
        { 1.f, 1.f,-1.f, 1.f,-1.f,-1.f},
        {-1.f, 1.f, 1.f,-1.f, 1.f, 1.f},
        { 1.f,-1.f,-1.f, 1.f, 1.f,-1.f},
        { 1.f,-1.f, 1.f,-1.f, 1.f,-1.f},
        {-1.f, 1.f,-1.f, 1.f, 1.f,-1.f},
        { 1.f, 1.f,-1.f,-1.f, 1.f,-1.f},
        {-1.f, 1.f, 1.f,-1.f,-1.f, 1.f},
        { 1.f,-1.f, 1.f, 1.f,-1.f,-1.f},
        {-1.f,-1.f, 1.f,-1.f, 1.f, 1.f},
        { 1.f,-1.f,-1.f, 1.f,-1.f, 1.f}
    };

    // Material-specific signed permutations following the normalized Hadamard
    // scatter.  A signed permutation is orthogonal, so the transform remains
    // energy-preserving while each material receives a different recurrence
    // pattern through the tank instead of sharing one generic FDN topology.
    static constexpr int scatterPerm[kMaterials][kCombs] = {
        {0,1,2,3,4,5,6,7},
        {0,2,4,6,1,3,5,7},
        {7,5,3,1,6,4,2,0},
        {0,3,6,1,4,7,2,5},
        {1,4,7,2,5,0,3,6},
        {2,5,0,3,6,1,4,7},
        {0,5,2,7,4,1,6,3},
        {3,0,5,2,7,4,1,6},
        {6,2,7,3,0,4,1,5},
        {4,0,6,2,5,1,7,3},
        {7,0,6,1,5,2,4,3}
    };
    static constexpr float scatterSign[kMaterials][kCombs] = {
        { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f},
        { 1.f,-1.f, 1.f,-1.f, 1.f,-1.f, 1.f,-1.f},
        {-1.f, 1.f, 1.f,-1.f, 1.f, 1.f,-1.f,-1.f},
        { 1.f, 1.f,-1.f, 1.f,-1.f,-1.f, 1.f,-1.f},
        {-1.f, 1.f,-1.f, 1.f,-1.f, 1.f,-1.f, 1.f},
        { 1.f,-1.f,-1.f, 1.f, 1.f,-1.f, 1.f,-1.f},
        { 1.f,-1.f, 1.f, 1.f,-1.f,-1.f, 1.f,-1.f},
        {-1.f, 1.f, 1.f,-1.f,-1.f, 1.f, 1.f,-1.f},
        { 1.f, 1.f,-1.f,-1.f, 1.f,-1.f,-1.f, 1.f},
        {-1.f,-1.f, 1.f, 1.f,-1.f, 1.f, 1.f,-1.f},
        { 1.f,-1.f,-1.f, 1.f,-1.f, 1.f, 1.f,-1.f}
    };

    for (int32 s = 0; s < data.numSamples; ++s)
    {
        // Apply every automation point whose sample offset has been reached.
        // Multiple points at the same offset resolve in host-provided queue order.
        for (int32 qIndex = 0; qIndex < activeQueues; ++qIndex)
        {
            while (pointIndex[qIndex] < pointCount[qIndex] &&
                   nextOffset[qIndex] <= s)
            {
                applyParameter(paramQueues[qIndex]->getParameterId(),
                               (float)nextValue[qIndex]);

                ++pointIndex[qIndex];
                if (pointIndex[qIndex] < pointCount[qIndex])
                {
                    if (paramQueues[qIndex]->getPoint(pointIndex[qIndex],
                                                     nextOffset[qIndex],
                                                     nextValue[qIndex]) != kResultTrue)
                    {
                        pointIndex[qIndex] = pointCount[qIndex];
                    }
                }
            }
        }

        const int mat = std::max(0, std::min(kMaterials - 1,
            (int)std::lround(material_ * (float)(kMaterials - 1))));

        const float xL = in[0][s];
        const float xR = in[1][s];

        smSize_ += smoothCoef * (size_ - smSize_);
        smDecay_ += smoothCoef * (decay_ - smDecay_);
        smPreDelay_ += smoothCoef * (preDelay_ - smPreDelay_);
        smDiffusion_ += smoothCoef * (diffusion_ - smDiffusion_);
        smDamping_ += smoothCoef * (damping_ - smDamping_);
        smMetal_ += smoothCoef * (metal_ - smMetal_);
        smClang_ += smoothCoef * (clang_ - smClang_);
        smRattle_ += smoothCoef * (rattle_ - smRattle_);
        smBody_ += smoothCoef * (body_ - smBody_);
        smWidth_ += smoothCoef * (width_ - smWidth_);
        smMix_ += smoothCoef * (mix_ - smMix_);
        smOutput_ += smoothCoef * (output_ - smOutput_);

        const float outGain = std::pow(10.f, ((smOutput_ * 24.f) - 12.f) / 20.f);

        // MIX is an actual wet/dry control.  The reverb core is calibrated
        // separately below; do not hide gain correction inside the knob law.
        const float wet = smMix_;
        const float dry = 1.f - wet;

        const float preSamples = std::max(0.f, std::min((float)preL_.size() - 2.f,
                               smPreDelay_ * 0.18f * (float)sampleRate_));
        preL_[(size_t)preWrite_] = xL;
        preR_[(size_t)preWrite_] = xR;

        const int pre0 = (int)std::floor(preSamples);
        const float preFrac = preSamples - (float)pre0;
        int pr0 = preWrite_ - pre0;
        while (pr0 < 0) pr0 += (int)preL_.size();
        int pr1 = pr0 - 1;
        if (pr1 < 0) pr1 += (int)preL_.size();

        const float pL = preL_[(size_t)pr0] * (1.f - preFrac) + preL_[(size_t)pr1] * preFrac;
        const float pR = preR_[(size_t)pr0] * (1.f - preFrac) + preR_[(size_t)pr1] * preFrac;
        if (++preWrite_ >= (int)preL_.size()) preWrite_ = 0;

        // Old-style excitation: mostly mono, preserving the artificial "one box" feel.
        const float mono = 0.5f * (pL + pR);
        const float side = 0.5f * (pL - pR);
        float exciteL = mono + side * (0.10f + 0.22f * smWidth_);
        float exciteR = mono - side * (0.10f + 0.22f * smWidth_);

        // V2 sparse onset cloud.  Read a handful of deterministic taps behind
        // the selected pre-delay position.  Diffusion raises their contribution;
        // Metal keeps them more exposed and less "polite".  No RNG is used, so
        // renders remain bit-repeatable for the same host/sample-rate conditions.
        float earlyL = 0.f;
        float earlyR = 0.f;
        const float earlyScale = 0.72f + 0.55f * smSize_;
        for (int e = 0; e < 6; ++e)
        {
            int tap = pr0 - (int)std::lround(
                earlyMs[mat][e] * earlyScale * 0.001f * (float)sampleRate_);
            while (tap < 0) tap += (int)preL_.size();
            const float weight = (0.22f - 0.022f * (float)e)
                               * (0.45f + 0.55f * smDiffusion_);
            const float sign = earlySign[mat][e];
            earlyL += preL_[(size_t)tap] * weight * sign;
            earlyR += preR_[(size_t)tap] * weight
                    * earlySign[mat][5 - e];
        }
        const float earlyDrive = 0.42f + 0.30f * smMetal_;
        exciteL += std::tanh(earlyL * earlyDrive);
        exciteR += std::tanh(earlyR * earlyDrive);

        float combSumL = 0.f;
        float combSumR = 0.f;

        const float sizeScale = 0.58f + smSize_ * 1.22f;
        const float bodySkew = 0.82f + smBody_ * 0.36f;
        const float rt60 = 0.45f * std::pow(28.f, smDecay_) * rt60Scale[mat];
        // Keep DAMPING strong at 100%, but avoid collapsing the tank into an
        // almost HF-dead state.  V2 uses a shallower one-pole range and leaves
        // material HF retention to do the finer spectral shaping.
        const float dampingCoef = 0.24f + (1.f - smDamping_) * 0.68f;

        std::array<float, kCombs> filteredL {};
        std::array<float, kCombs> filteredR {};
        std::array<float, kCombs> feedbackSourceL {};
        std::array<float, kCombs> feedbackSourceR {};
        std::array<float, kCombs> delaySeconds {};
        std::array<float, kCombs> feedbackGain {};

        // Read all delay paths first.  V2 then scatters their feedback through a
        // normalized Hadamard matrix before writing the next sample.  This turns
        // the old bank of mostly independent resonators into a compact FDN-like
        // network while preserving the deliberately exposed modal character.
        for (int i = 0; i < kCombs; ++i)
        {
            rattlePhase_[i] += (2.f * kPi * (0.13f + 0.037f * i)) / (float)sampleRate_;
            if (rattlePhase_[i] >= 2.f * kPi) rattlePhase_[i] -= 2.f * kPi;

            float ms = lerp(baseMs[mat][i], uglyMs[mat][i], smMetal_);
            ms *= sizeScale;
            ms *= (i & 1) ? (1.f / bodySkew) : bodySkew;

            const float rattleMask = (i == 1 || i == 4 || i == 6) ? 1.f : 0.25f;
            const float phase = rattlePhase_[i];
            const float rattleJitter = smRattle_ * rattleMask * 0.0035f * (float)sampleRate_
                               * (std::sin(phase) + 0.31f * std::sin(phase * 2.7f + i));
            const float materialMotion = intrinsicMotion[mat] * (float)sampleRate_
                               * (std::sin(phase * motionRate[mat] + 0.37f * i)
                               + 0.23f * std::sin(phase * motionRate[mat] * 2.31f + i));
            const float delayL = ms * 0.001f * (float)sampleRate_ + rattleJitter + materialMotion;
            const float delayR = delayL + (17.f + 3.f * (float)i);

            const float yL = combL_[i].read(delayL);
            const float yR = combR_[i].read(delayR);

            combL_[i].lp = zapTiny(combL_[i].lp + dampingCoef * (yL - combL_[i].lp));
            combR_[i].lp = zapTiny(combR_[i].lp + dampingCoef * (yR - combR_[i].lp));
            filteredL[(size_t)i] = combL_[i].lp;
            filteredR[(size_t)i] = combR_[i].lp;

            // Split the delay output into a slowly varying body component and
            // a residual high-frequency component.  Damping now controls how
            // much of that HF residual survives each trip through the tank.
            // METAL deliberately restores some brightness at high settings.
            const float hfRetention = std::max(0.02f, std::min(0.98f,
                hfMaterial[mat] * (1.f - 0.88f * smDamping_)
                + 0.16f * smMetal_ * (1.f - 0.55f * smDamping_)));
            feedbackSourceL[(size_t)i] =
                filteredL[(size_t)i] + (yL - filteredL[(size_t)i]) * hfRetention;
            feedbackSourceR[(size_t)i] =
                filteredR[(size_t)i] + (yR - filteredR[(size_t)i]) * hfRetention;

            delaySeconds[(size_t)i] = std::max(0.001f, ms * 0.001f);
            float fb = std::pow(10.f, -3.f * delaySeconds[(size_t)i] / rt60);
            fb += smClang_ * clangShape[mat][i] * clangDepth[mat];
            feedbackGain[(size_t)i] = std::max(0.20f, std::min(0.991f, fb));

            const float weight = combWeight[mat][i];
            combSumL += filteredL[(size_t)i] * weight;
            combSumR += filteredR[(size_t)i] * weight;
        }

        std::array<float, kCombs> scatteredL {};
        std::array<float, kCombs> scatteredR {};
        hadamard8(feedbackSourceL, scatteredL);
        hadamard8(feedbackSourceR, scatteredR);

        std::array<float, kCombs> materialScatterL {};
        std::array<float, kCombs> materialScatterR {};
        for (int i = 0; i < kCombs; ++i)
        {
            const int src = scatterPerm[mat][i];
            const float sign = scatterSign[mat][i];
            materialScatterL[(size_t)i] = scatteredL[(size_t)src] * sign;
            materialScatterR[(size_t)i] = scatteredR[(size_t)src] * sign;
        }
        scatteredL = materialScatterL;
        scatteredR = materialScatterR;

        // Energy-preserving stereo rotation inside the feedback network.  This
        // creates decorrelation in the tank itself instead of relying only on
        // output M/S widening.  The orthogonal rotation cannot increase vector
        // energy, which keeps the feedback stability argument intact.
        const float stereoAngle = smWidth_ * 0.42f; // radians, intentionally moderate
        const float stereoCos = std::cos(stereoAngle);
        const float stereoSin = std::sin(stereoAngle);
        for (int i = 0; i < kCombs; ++i)
        {
            const float a = scatteredL[(size_t)i];
            const float b = scatteredR[(size_t)i];
            scatteredL[(size_t)i] = stereoCos * a + stereoSin * b;
            scatteredR[(size_t)i] = stereoCos * b - stereoSin * a;
        }

        // DIFFUSION now also controls feedback scattering.  Low values retain
        // the coarse V1-style modal bank; higher values increase echo density
        // without erasing the intentionally metallic identity.
        const float scatterBlend = std::max(0.f, std::min(0.78f,
            smDiffusion_ * (0.58f + 0.20f * (1.f - smMetal_))));

        for (int i = 0; i < kCombs; ++i)
        {
            const float drive = 1.f + smMetal_ * 2.2f + smClang_ * 1.6f;
            const float injectGain = 0.20f + 0.055f * i;
            const float drivenL = std::tanh(exciteL * injectGain * drive);
            const float drivenR = std::tanh(exciteR * injectGain * drive);

            const float localL = feedbackSourceL[(size_t)i] * feedbackGain[(size_t)i];
            const float localR = feedbackSourceR[(size_t)i] * feedbackGain[(size_t)i];
            const float fdnL = scatteredL[(size_t)i] * feedbackGain[(size_t)i];
            const float fdnR = scatteredR[(size_t)i] * feedbackGain[(size_t)i];

            const float writeL = std::tanh(drivenL + lerp(localL, fdnL, scatterBlend));
            const float writeR = std::tanh(drivenR + lerp(localR, fdnR, scatterBlend));

            combL_[i].push(processDigital(writeL));
            combR_[i].push(processDigital(writeR));
        }

        combSumL *= 0.17f;
        combSumR *= 0.17f;

        // Short serial allpasses make the parallel echoes fuse into reverb,
        // but intentionally stop before the tail becomes modern/smooth.
        float apOutL = combSumL;
        float apOutR = combSumR;

        // DIFFUSION's main job is echo density: it progressively blends in the
        // serial stages.  A smaller secondary feedback contribution deliberately
        // preserves the musical "metalizer" interaction discovered by ear.
        const float apFeedback = std::min(0.78f,
            0.42f + smMetal_ * 0.12f + smDiffusion_ * 0.10f
            + diffusionBias[mat] * 0.35f);
        for (int i = 0; i < kAllpasses; ++i)
        {
            const float uglyScale = 1.f - smMetal_ * (0.10f + 0.035f * i);
            const float dL = apMs[mat][i] * uglyScale * 0.001f * (float)sampleRate_;
            const float dR = dL + 11.f + 4.f * (float)i;

            const float stageL = processAllpass(apL_[i], apOutL, dL, apFeedback);
            const float stageR = processAllpass(apR_[i], apOutR, dR, apFeedback);

            // Earlier stages enter first; later stages require progressively more
            // Diffusion.  Smooth ramps avoid switching/clicking while preserving
            // the deliberately coarse low-Diffusion character.
            const float threshold = 0.12f + 0.18f * (float)i;
            const float stageMix = std::max(0.f, std::min(1.f,
                (smDiffusion_ - threshold) / 0.28f));
            apOutL = lerp(apOutL, stageL, stageMix);
            apOutR = lerp(apOutR, stageR, stageMix);
        }

        // At high METAL/CLANG the raw comb bank is deliberately leaked back in.
        // This is the "too metallic for a good reverb" control range.
        const float rawLeak = std::min(0.82f,
            smMetal_ * (0.18f + 0.50f * smClang_) * rawLeakScale[mat]);
        float wetL = apOutL * (1.f - rawLeak) + combSumL * rawLeak;
        float wetR = apOutR * (1.f - rawLeak) + combSumR * rawLeak;

        const float characterGain = (1.12f + 1.25f * smMetal_ + 0.90f * smClang_) * materialGain[mat];
        wetL *= characterGain;
        wetR *= characterGain;

        // Calibrate the deliberately hot character core independently from MIX.
        // -12 dB keeps full-wet usable while allowing ordinary 10-20% insert values
        // to behave like ordinary reverb mix settings.
        constexpr float kWetCalibration = 0.25f; // ~ -12.0 dB
        wetL *= kWetCalibration;
        wetR *= kWetCalibration;

        const float wmid = 0.5f * (wetL + wetR);
        const float wside = 0.5f * (wetL - wetR) * (0.18f + smWidth_ * 1.82f);
        wetL = wmid + wside;
        wetR = wmid - wside;

        if (bypass_)
        {
            out[0][s] = xL;
            out[1][s] = xR;
        }
        else
        {
            out[0][s] = (xL * dry + wetL * wet) * outGain;
            out[1][s] = (xR * dry + wetR * wet) * outGain;
        }
    }

    // A host may place a parameter point exactly at the block boundary.
    // It affects no sample in this block, but must become the target state for
    // the following block (and for processor/controller synchronization).
    consumeRemainingParameterPoints();

    return kResultOk;
}

tresult PLUGIN_API Processor::setState(IBStream* state)
{
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);

    float values[kComponentStateValueCount] {};
    int32 bp = 0;
    if (!readComponentStatePayload(s, values, bp))
        return kResultFalse;

    const ParamID ids[kComponentStateValueCount] = {
        kMaterial,kSize,kDecay,kPreDelay,kDiffusion,kDamping,kMetal,kClang,
        kRattle,kBody,kWidth,kMix,kOutput,kDigital
    };
    for (int i = 0; i < kComponentStateValueCount; ++i)
        applyParameter(ids[i], values[i]);
    bypass_ = bp != 0;
    resetSmoothers();
    return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* state)
{
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    const float values[kComponentStateValueCount] = {
        material_, size_, decay_, preDelay_, diffusion_, damping_, metal_, clang_,
        rattle_, body_, width_, mix_, output_, digital_
    };
    return writeComponentStatePayload(s, values, bypass_ ? 1 : 0) ? kResultOk : kResultFalse;
}

} // namespace UglyReverb
