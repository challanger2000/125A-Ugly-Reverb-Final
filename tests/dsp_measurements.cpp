#include "processor.h"
#include "parameters.h"
#include "state_format.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using UglyReverb::Processor;

namespace {

struct RenderResult
{
    std::vector<float> left;
    std::vector<float> right;
};

bool finiteBuffer(const std::vector<float>& x)
{
    for (float v : x)
        if (!std::isfinite(v)) return false;
    return true;
}

double energy(const std::vector<float>& x, size_t a, size_t b)
{
    b = std::min(b, x.size());
    a = std::min(a, b);
    double e = 0.0;
    for (size_t i = a; i < b; ++i) e += (double)x[i] * (double)x[i];
    return e;
}

double meanSquare(const std::vector<float>& x, size_t a, size_t b)
{
    b = std::min(b, x.size());
    a = std::min(a, b);
    if (b <= a) return 0.0;
    return energy(x, a, b) / (double)(b - a);
}

double difference(const std::vector<float>& a, const std::vector<float>& b)
{
    const size_t n = std::min(a.size(), b.size());
    double d = 0.0;
    for (size_t i=0;i<n;++i) d += std::fabs((double)a[i]-(double)b[i]);
    return d / std::max<size_t>(1,n);
}

RenderResult render(double sr, double seconds, float material, float preDelay, float digital,
                    bool bypass=false, bool impulse=true, int block=128, float decay=0.58f,
                    float metal=0.68f, float clang=0.55f, float damping=0.48f,
                    float rattle=0.12f, float diffusion=0.45f, float body=0.55f,
                    float mix=1.f)
{
    block = std::max(1, block);
    Processor p;
    if (p.initialize(nullptr) != kResultOk)
        throw std::runtime_error("Processor initialize failed");

    ProcessSetup setup {};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = block;
    setup.sampleRate = sr;
    if (p.setupProcessing(setup) != kResultOk)
        throw std::runtime_error("setupProcessing failed");

    // Set the initial component state before activation, as a host normally does.
    // Activation then initializes all smoothing states from these target values.
    p.setTestParameter(UglyReverb::kMaterial, material);
    p.setTestParameter(UglyReverb::kPreDelay, preDelay);
    p.setTestParameter(UglyReverb::kDigital, digital);
    p.setTestParameter(UglyReverb::kDecay, decay);
    p.setTestParameter(UglyReverb::kMetal, metal);
    p.setTestParameter(UglyReverb::kClang, clang);
    p.setTestParameter(UglyReverb::kDamping, damping);
    p.setTestParameter(UglyReverb::kRattle, rattle);
    p.setTestParameter(UglyReverb::kDiffusion, diffusion);
    p.setTestParameter(UglyReverb::kBody, body);
    p.setTestParameter(UglyReverb::kMix, bypass ? 0.28f : mix);
    p.setTestParameter(UglyReverb::kOutput, 0.5f);
    p.setTestParameter(UglyReverb::kBypass, bypass ? 1.f : 0.f);

    if (p.setActive(true) != kResultOk)
        throw std::runtime_error("setActive failed");

    const size_t total=(size_t)std::llround(sr*seconds);
    RenderResult rr;
    rr.left.assign(total,0.f);
    rr.right.assign(total,0.f);

    std::vector<float> inL(block,0.f), inR(block,0.f), outL(block,0.f), outR(block,0.f);
    float* inPtrs[2]={inL.data(),inR.data()};
    float* outPtrs[2]={outL.data(),outR.data()};

    AudioBusBuffers inBus {};
    inBus.numChannels=2;
    inBus.channelBuffers32=inPtrs;
    AudioBusBuffers outBus {};
    outBus.numChannels=2;
    outBus.channelBuffers32=outPtrs;

    bool sent=false;
    size_t pos=0;
    while(pos<total)
    {
        const int n=(int)std::min<size_t>(block,total-pos);
        std::fill(inL.begin(),inL.end(),0.f);
        std::fill(inR.begin(),inR.end(),0.f);
        std::fill(outL.begin(),outL.end(),0.f);
        std::fill(outR.begin(),outR.end(),0.f);
        if (impulse && !sent)
        {
            inL[0]=1.f;
            inR[0]=1.f;
            sent=true;
        }

        ProcessData data {};
        data.processMode=kRealtime;
        data.symbolicSampleSize=kSample32;
        data.numSamples=n;
        data.numInputs=1;
        data.numOutputs=1;
        data.inputs=&inBus;
        data.outputs=&outBus;

        if (p.process(data) != kResultOk)
            throw std::runtime_error("process failed");

        for(int i=0;i<n;++i)
        {
            rr.left[pos+i]=outL[i];
            rr.right[pos+i]=outR[i];
        }
        pos+=(size_t)n;
    }

    p.setActive(false);
    p.terminate();
    return rr;
}

RenderResult renderMaterialAutomationAtZero(double sr, double seconds, float material)
{
    constexpr int block = 128;
    Processor p;
    if (p.initialize(nullptr) != kResultOk)
        throw std::runtime_error("Processor initialize failed");

    ProcessSetup setup {};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = block;
    setup.sampleRate = sr;
    if (p.setupProcessing(setup) != kResultOk)
        throw std::runtime_error("setupProcessing failed");
    p.setTestParameter(UglyReverb::kPreDelay, 0.f);
    p.setTestParameter(UglyReverb::kMix, 1.f);
    p.setTestParameter(UglyReverb::kOutput, 0.5f);
    if (p.setActive(true) != kResultOk)
        throw std::runtime_error("setActive failed");

    const size_t total=(size_t)std::llround(sr*seconds);
    RenderResult rr;
    rr.left.assign(total,0.f);
    rr.right.assign(total,0.f);

    std::vector<float> inL(block,0.f), inR(block,0.f), outL(block,0.f), outR(block,0.f);
    float* inPtrs[2]={inL.data(),inR.data()};
    float* outPtrs[2]={outL.data(),outR.data()};
    AudioBusBuffers inBus {}; inBus.numChannels=2; inBus.channelBuffers32=inPtrs;
    AudioBusBuffers outBus {}; outBus.numChannels=2; outBus.channelBuffers32=outPtrs;

    bool first=true;
    size_t pos=0;
    while(pos<total)
    {
        const int n=(int)std::min<size_t>(block,total-pos);
        std::fill(inL.begin(),inL.end(),0.f);
        std::fill(inR.begin(),inR.end(),0.f);
        std::fill(outL.begin(),outL.end(),0.f);
        std::fill(outR.begin(),outR.end(),0.f);
        if(first) { inL[0]=1.f; inR[0]=1.f; }

        ParameterChanges changes(1);
        int32 queueIndex=0;
        if(first)
        {
            auto* q=changes.addParameterData(UglyReverb::kMaterial,queueIndex);
            int32 point=0;
            q->addPoint(0,material,point);
        }

        ProcessData data {};
        data.processMode=kRealtime;
        data.symbolicSampleSize=kSample32;
        data.numSamples=n;
        data.numInputs=1;
        data.numOutputs=1;
        data.inputs=&inBus;
        data.outputs=&outBus;
        data.inputParameterChanges=first?&changes:nullptr;

        if(p.process(data)!=kResultOk)
            throw std::runtime_error("process failed");

        for(int i=0;i<n;++i)
        {
            rr.left[pos+i]=outL[i];
            rr.right[pos+i]=outR[i];
        }
        first=false;
        pos+=(size_t)n;
    }

    p.setActive(false);
    p.terminate();
    return rr;
}

void require(bool cond, const std::string& msg, int& failures)
{
    if(cond) std::cout << "[PASS] " << msg << "\n";
    else { std::cout << "[FAIL] " << msg << "\n"; ++failures; }
}

}

int main()
{
    int failures=0;
    try
    {
        for(double sr : {44100.0,48000.0,96000.0})
        {
            auto r=render(sr,4.0,0.5f,0.f,0.f);
            require(finiteBuffer(r.left)&&finiteBuffer(r.right),
                    "Finite output at "+std::to_string((int)sr)+" Hz", failures);

            float peak=0.f;
            for(float v:r.left) peak=std::max(peak,std::fabs(v));
            require(peak < 2.0f, "Bounded peak at "+std::to_string((int)sr)+" Hz", failures);

            const size_t s100=(size_t)(sr*0.10);
            const size_t s600=(size_t)(sr*0.60);
            const size_t s3000=(size_t)(sr*3.0);
            const size_t s3500=(size_t)(sr*3.5);
            const double early=meanSquare(r.left,s100,s600);
            const double late=meanSquare(r.left,s3000,s3500);
            std::cout << "[INFO] " << (int)sr << " Hz early_ms=" << early
                      << " late_ms=" << late
                      << " ratio=" << (early > 0.0 ? late / early : 0.0) << "\n";
            require(early > 1e-10, "Audible reverb tail energy", failures);
            require(late < early, "Tail average power decays over time", failures);
        }

        auto silence=render(48000.0,1.0,0.5f,0.f,0.f,false,false);
        require(energy(silence.left,0,silence.left.size()) < 1e-20 &&
                energy(silence.right,0,silence.right.size()) < 1e-20,
                "Silence in -> silence out", failures);

        auto delayed=render(48000.0,1.0,0.5f,0.5f,0.f);
        const size_t first70=(size_t)(48000.0*0.070);
        require(energy(delayed.left,0,first70) < 1e-12,
                "Pre-delay prevents premature wet output", failures);

        const float materialValues[] = {0.f,0.1f,0.2f,0.3f,0.4f,0.5f,0.6f,0.7f,0.8f,0.9f,1.f};
        const char* materialNames[] = {"Plate","Thin Plate","Heavy Plate","Sheet","Spring","Steel",
                                       "Pipe","Metal Drum","Oil Can","Chamber","Tank"};
        std::vector<RenderResult> materials;
        for (float m : materialValues) materials.push_back(render(48000.0,1.5,m,0.f,0.f));
        for (int i=0;i<10;++i)
            require(difference(materials[i].left,materials[i+1].left) > 1e-5,
                    std::string(materialNames[i])+" differs from "+materialNames[i+1], failures);

        // Legacy anchors remain exact after expanding the stepped Material parameter.
        auto legacyPlate=render(48000.0,1.5,0.f,0.f,0.f);
        auto legacySteel=render(48000.0,1.5,0.5f,0.f,0.f);
        auto legacyTank=render(48000.0,1.5,1.f,0.f,0.f);
        require(difference(legacyPlate.left,materials[0].left) < 1e-7,
                "Legacy Material 0.0 still resolves to Plate", failures);
        require(difference(legacySteel.left,materials[5].left) < 1e-7,
                "Legacy Material 0.5 still resolves to Steel", failures);
        require(difference(legacyTank.left,materials[10].left) < 1e-7,
                "Legacy Material 1.0 still resolves to Tank", failures);

        auto shortDecay=render(48000.0,3.0,0.5f,0.f,0.f,false,true,128,0.15f);
        auto longDecay=render(48000.0,3.0,0.5f,0.f,0.f,false,true,128,0.90f);
        const double shortLate=energy(shortDecay.left,(size_t)(48000.0*1.5),shortDecay.left.size());
        const double longLate=energy(longDecay.left,(size_t)(48000.0*1.5),longDecay.left.size());
        require(longLate > shortLate * 10.0, "Decay control increases late-tail energy", failures);

        auto restrained=render(48000.0,2.5,0.5f,0.f,0.f,false,true,128,
                                0.55f,0.10f,0.10f,0.70f,0.0f,0.45f,0.55f);
        auto metalOnly=render(48000.0,2.5,0.5f,0.f,0.f,false,true,128,
                              0.55f,1.0f,0.0f,0.70f,0.0f,0.45f,0.55f);
        auto clangHeavy=render(48000.0,2.5,0.5f,0.f,0.f,false,true,128,
                               1.0f,1.0f,1.0f,0.0f,0.0f,0.55f,0.55f);
        auto fullChaos=render(48000.0,2.5,0.5f,0.f,0.f,false,true,128,
                              1.0f,1.0f,1.0f,0.0f,1.0f,0.55f,0.55f);

        const size_t characterStart=(size_t)(48000.0*0.15);
        const double restrainedWet=energy(restrained.left,characterStart,restrained.left.size());
        const double clangWet=energy(clangHeavy.left,characterStart,clangHeavy.left.size());
        const double metalDelta=difference(restrained.left,metalOnly.left);
        const double clangDelta=difference(metalOnly.left,clangHeavy.left);
        const double rattleDelta=difference(clangHeavy.left,fullChaos.left);
        std::cout << "[INFO] restrained_character_energy=" << restrainedWet
                  << " clang_character_energy=" << clangWet
                  << " ratio=" << (restrainedWet > 0.0 ? clangWet/restrainedWet : 0.0)
                  << " metal_delta=" << metalDelta
                  << " clang_delta=" << clangDelta
                  << " rattle_delta=" << rattleDelta << "\n";
        require(metalDelta > 5e-5, "Metal alone substantially changes the reverb body", failures);
        require(clangDelta > 1e-4, "Clang substantially reshapes the metallic tail", failures);
        require(clangWet > restrainedWet * 1.5, "Metal plus Clang increases character-tail energy", failures);
        require(rattleDelta > 1e-4, "Full Rattle substantially changes the resonant structure", failures);

        // V2 feedback scattering: Diffusion now changes both the serial diffuser
        // and the feedback topology.  Verify that the resulting tank response is
        // materially different while remaining deterministic and bounded.
        auto lowScatter=render(48000.0,2.5,0.5f,0.f,0.f,false,true,128,
                               0.72f,0.62f,0.42f,0.52f,0.08f,0.0f,0.55f);
        auto highScatter=render(48000.0,2.5,0.5f,0.f,0.f,false,true,128,
                                0.72f,0.62f,0.42f,0.52f,0.08f,1.0f,0.55f);
        const double scatterDelta=difference(lowScatter.left,highScatter.left);
        std::cout << "[INFO] v2_scattering_delta=" << scatterDelta << "\n";
        require(scatterDelta > 1e-4,
                "V2 Diffusion/scattering materially changes the tank response", failures);
        require(finiteBuffer(lowScatter.left) && finiteBuffer(highScatter.left),
                "V2 feedback scattering remains finite", failures);

        auto scatterBlock1=render(48000.0,1.5,0.5f,0.f,0.f,false,true,1,
                                  0.72f,0.62f,0.42f,0.52f,0.08f,0.85f,0.55f);
        auto scatterBlock512=render(48000.0,1.5,0.5f,0.f,0.f,false,true,512,
                                    0.72f,0.62f,0.42f,0.52f,0.08f,0.85f,0.55f);
        require(difference(scatterBlock1.left,scatterBlock512.left) < 1e-7,
                "V2 scattering render is block-size deterministic", failures);

        // MIX calibration: dry must be exact at zero, and reverb-tail energy
        // must rise predictably through ordinary insert values.
        auto mix0  = render(48000.0,2.0,0.f,0.f,0.f,false,true,128,0.58f,0.68f,0.55f,0.48f,0.12f,0.45f,0.55f,0.f);
        auto mix10 = render(48000.0,2.0,0.f,0.f,0.f,false,true,128,0.58f,0.68f,0.55f,0.48f,0.12f,0.45f,0.55f,0.10f);
        auto mix20 = render(48000.0,2.0,0.f,0.f,0.f,false,true,128,0.58f,0.68f,0.55f,0.48f,0.12f,0.45f,0.55f,0.20f);
        auto mix50 = render(48000.0,2.0,0.f,0.f,0.f,false,true,128,0.58f,0.68f,0.55f,0.48f,0.12f,0.45f,0.55f,0.50f);
        auto mix100= render(48000.0,2.0,0.f,0.f,0.f,false,true,128,0.58f,0.68f,0.55f,0.48f,0.12f,0.45f,0.55f,1.f);
        const size_t mixTail=(size_t)(48000.0*0.08);
        const double e0=energy(mix0.left,mixTail,mix0.left.size());
        const double e10=energy(mix10.left,mixTail,mix10.left.size());
        const double e20=energy(mix20.left,mixTail,mix20.left.size());
        const double e50=energy(mix50.left,mixTail,mix50.left.size());
        const double e100=energy(mix100.left,mixTail,mix100.left.size());
        require(e0 < 1e-20, "Mix 0% produces no wet tail", failures);
        require(e10 > e0 && e20 > e10 && e50 > e20 && e100 > e50,
                "Mix wet-tail energy rises monotonically 0/10/20/50/100%", failures);
        require(e20/e10 > 3.5 && e20/e10 < 4.5,
                "Linear Mix law gives expected 20% vs 10% wet-tail ratio", failures);
        require(e50/e20 > 5.5 && e50/e20 < 7.0,
                "Linear Mix law gives expected 50% vs 20% wet-tail ratio", failures);

        auto clean=render(48000.0,1.5,0.5f,0.f,0.f);
        auto bit12=render(48000.0,1.5,0.5f,0.f,0.5f);
        auto bit8=render(48000.0,1.5,0.5f,0.f,1.f);
        require(difference(clean.left,bit12.left) > 1e-7, "12-bit color changes tail", failures);
        require(difference(bit12.left,bit8.left) > 1e-7, "8-bit differs from 12-bit", failures);

        for (int block : {1, 17, 64, 127, 512})
        {
            auto r = render(48000.0, 1.5, 0.5f, 0.f, 0.f, false, true, block);
            require(finiteBuffer(r.left) && finiteBuffer(r.right),
                    "Finite output with block size " + std::to_string(block), failures);
        }

        Processor p;
        p.initialize(nullptr);
        ProcessSetup stateSetup {};
        stateSetup.processMode = kRealtime;
        stateSetup.symbolicSampleSize = kSample32;
        stateSetup.maxSamplesPerBlock = 128;
        stateSetup.sampleRate = 48000.0;
        p.setupProcessing(stateSetup);
        require(p.getLatencySamples()==0, "Reported latency is 0 samples", failures);
        require(p.getTailSamples()==7200000u, "Reported reverb tail is 150 seconds at 48 kHz", failures);

        Steinberg::MemoryStream state;
        p.setTestParameter(UglyReverb::kDecay, 0.93f);
        p.setTestParameter(UglyReverb::kMetal, 0.81f);
        p.setTestParameter(UglyReverb::kMix, 0.67f);
        require(p.getState(&state)==kResultOk, "State serialization succeeds", failures);
        state.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        Steinberg::IBStreamer stateReader(&state, kLittleEndian);
        Steinberg::int32 stateMagic=0, stateVersion=0;
        require(stateReader.readInt32(stateMagic) && stateMagic==UglyReverb::kComponentStateMagic,
                "State serialization writes component magic", failures);
        require(stateReader.readInt32(stateVersion) && stateVersion==UglyReverb::kComponentStateVersion,
                "State serialization writes component version", failures);
        state.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        Processor restored;
        restored.initialize(nullptr);
        require(restored.setState(&state)==kResultOk, "State restore succeeds", failures);
        Steinberg::MemoryStream roundtrip;
        require(restored.getState(&roundtrip)==kResultOk, "Restored state serializes again", failures);
        restored.terminate();

        // Legacy pre-version state (14 floats + bypass) must remain loadable.
        Steinberg::MemoryStream legacyState;
        Steinberg::IBStreamer legacyWriter(&legacyState, kLittleEndian);
        const float legacyValues[UglyReverb::kComponentStateValueCount] = {
            1.0f,0.55f,0.93f,0.08f,0.45f,0.48f,0.81f,0.55f,
            0.12f,0.55f,0.75f,0.67f,0.5f,0.0f
        };
        for(float value:legacyValues) legacyWriter.writeFloat(value);
        legacyWriter.writeInt32(1);
        legacyState.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        Processor legacyRestored;
        legacyRestored.initialize(nullptr);
        require(legacyRestored.setState(&legacyState)==kResultOk,
                "Legacy unversioned component state still restores", failures);
        Steinberg::MemoryStream migratedState;
        require(legacyRestored.getState(&migratedState)==kResultOk,
                "Legacy state migrates to versioned serialization", failures);
        legacyRestored.terminate();
        p.terminate();

        // Material automation at offset 0 must affect the same sample/block as a preset value.
        auto automatedTank=renderMaterialAutomationAtZero(48000.0,1.5,1.0f);
        auto referenceTank=render(48000.0,1.5,1.0f,0.f,0.f,false,true,128,
                                  0.58f,0.68f,0.55f,0.48f,0.12f,0.45f,0.55f,1.f);
        require(difference(automatedTank.left,referenceTank.left)<1e-7,
                "Material automation at sample 0 uses the new material in the same block", failures);

        // VST3 automation points must take effect at their exact sample offset.
        {
            Processor automated;
            automated.initialize(nullptr);
            ProcessSetup setup {};
            setup.processMode = kRealtime;
            setup.symbolicSampleSize = kSample32;
            setup.maxSamplesPerBlock = 128;
            setup.sampleRate = 48000.0;
            automated.setupProcessing(setup);
            automated.setActive(true);

            float inL[128], inR[128], outL[128] {}, outR[128] {};
            std::fill(std::begin(inL), std::end(inL), 0.25f);
            std::fill(std::begin(inR), std::end(inR), 0.25f);
            float* inPtrs[2] = {inL, inR};
            float* outPtrs[2] = {outL, outR};

            AudioBusBuffers inBus {};
            inBus.numChannels = 2;
            inBus.channelBuffers32 = inPtrs;
            AudioBusBuffers outBus {};
            outBus.numChannels = 2;
            outBus.channelBuffers32 = outPtrs;

            ParameterChanges changes(1);
            int32 queueIndex = 0;
            auto* q = changes.addParameterData(UglyReverb::kBypass, queueIndex);
            int32 point = 0;
            q->addPoint(64, 1.0, point);

            ProcessData data {};
            data.processMode = kRealtime;
            data.symbolicSampleSize = kSample32;
            data.numSamples = 128;
            data.numInputs = 1;
            data.numOutputs = 1;
            data.inputs = &inBus;
            data.outputs = &outBus;
            data.inputParameterChanges = &changes;

            require(automated.process(data) == kResultOk,
                    "Mid-block parameter automation processes successfully", failures);
            require(std::fabs(outL[63] - 0.25f) > 1e-5f,
                    "Bypass is still off immediately before automation offset", failures);
            require(outL[64] == 0.25f && outR[64] == 0.25f,
                    "Bypass automation takes effect at exact sample offset", failures);
            require(outL[127] == 0.25f && outR[127] == 0.25f,
                    "Bypass remains exact after automated transition", failures);

            automated.setActive(false);
            automated.terminate();
        }

        // Positive-length parameter-only blocks must still consume automation.
        {
            Processor paramOnly;
            paramOnly.initialize(nullptr);
            ProcessSetup setup {};
            setup.processMode=kRealtime;
            setup.symbolicSampleSize=kSample32;
            setup.maxSamplesPerBlock=64;
            setup.sampleRate=48000.0;
            paramOnly.setupProcessing(setup);
            paramOnly.setActive(true);

            ParameterChanges changes(1);
            int32 queueIndex=0;
            auto* q=changes.addParameterData(UglyReverb::kBypass,queueIndex);
            int32 point=0;
            q->addPoint(32,1.0,point);

            ProcessData noAudio {};
            noAudio.processMode=kRealtime;
            noAudio.symbolicSampleSize=kSample32;
            noAudio.numSamples=64;
            noAudio.numInputs=0;
            noAudio.numOutputs=0;
            noAudio.inputParameterChanges=&changes;
            require(paramOnly.process(noAudio)==kResultOk,
                    "Positive-length parameter-only block processes successfully", failures);

            float inL[1]={0.25f}, inR[1]={0.25f}, outL[1]={0.f}, outR[1]={0.f};
            float* inPtrs[2]={inL,inR};
            float* outPtrs[2]={outL,outR};
            AudioBusBuffers inBus {}; inBus.numChannels=2; inBus.channelBuffers32=inPtrs;
            AudioBusBuffers outBus {}; outBus.numChannels=2; outBus.channelBuffers32=outPtrs;
            ProcessData audio {};
            audio.processMode=kRealtime;
            audio.symbolicSampleSize=kSample32;
            audio.numSamples=1;
            audio.numInputs=1;
            audio.numOutputs=1;
            audio.inputs=&inBus;
            audio.outputs=&outBus;
            require(paramOnly.process(audio)==kResultOk && outL[0]==0.25f && outR[0]==0.25f,
                    "Parameter-only block updates bypass state for following audio", failures);

            paramOnly.setActive(false);
            paramOnly.terminate();
        }

        // Restarting processing must clear an old reverb tail without allocating new buffers.
        {
            Processor restart;
            restart.initialize(nullptr);
            ProcessSetup setup {};
            setup.processMode=kRealtime;
            setup.symbolicSampleSize=kSample32;
            setup.maxSamplesPerBlock=128;
            setup.sampleRate=48000.0;
            restart.setupProcessing(setup);
            restart.setTestParameter(UglyReverb::kMix,1.f);
            restart.setActive(true);
            restart.setProcessing(true);

            float inL[128] {}, inR[128] {}, outL[128] {}, outR[128] {};
            inL[0]=1.f; inR[0]=1.f;
            float* inPtrs[2]={inL,inR}; float* outPtrs[2]={outL,outR};
            AudioBusBuffers inBus {}; inBus.numChannels=2; inBus.channelBuffers32=inPtrs;
            AudioBusBuffers outBus {}; outBus.numChannels=2; outBus.channelBuffers32=outPtrs;
            ProcessData data {};
            data.processMode=kRealtime; data.symbolicSampleSize=kSample32; data.numSamples=128;
            data.numInputs=1; data.numOutputs=1; data.inputs=&inBus; data.outputs=&outBus;
            restart.process(data);

            for(int block=0;block<40;++block)
            {
                std::fill(std::begin(inL),std::end(inL),0.f);
                std::fill(std::begin(inR),std::end(inR),0.f);
                restart.process(data);
            }

            restart.setProcessing(false);
            require(restart.setProcessing(true)==kResultTrue,
                    "setProcessing restart is acknowledged", failures);
            std::fill(std::begin(outL),std::end(outL),0.f);
            std::fill(std::begin(outR),std::end(outR),0.f);
            require(restart.process(data)==kResultOk,
                    "Audio processes after setProcessing restart", failures);
            double restartEnergy=0.0;
            for(float v:outL) restartEnergy+=(double)v*(double)v;
            for(float v:outR) restartEnergy+=(double)v*(double)v;
            require(restartEnergy<1e-20,
                    "setProcessing(true) clears the previous reverb tail", failures);

            restart.setProcessing(false);
            restart.setActive(false);
            restart.terminate();
        }

        // Bypass must be exact for a one-sample impulse.
        auto bypass=render(48000.0,0.1,0.5f,0.f,0.f,true,true);
        require(bypass.left[0]==1.f && bypass.right[0]==1.f, "Bypass passes input sample exactly", failures);
        require(energy(bypass.left,1,bypass.left.size())==0.0, "Bypass adds no output tail", failures);

        for (float material : {0.f,0.1f,0.2f,0.3f,0.4f,0.5f,0.6f,0.7f,0.8f,0.9f,1.f})
        {
            auto extreme = render(96000.0, 6.0, material, 1.f, 1.f, false, true, 128,
                                  1.f, 1.f, 1.f, 0.f, 1.f, 0.55f, 0.55f);
            require(finiteBuffer(extreme.left) && finiteBuffer(extreme.right),
                    "Extreme settings remain finite for material " + std::to_string(material), failures);
            float peak = 0.f;
            for (float v : extreme.left) peak = std::max(peak, std::fabs(v));
            require(peak < 2.0f, "Extreme settings remain bounded", failures);
        }
    }
    catch(const std::exception& e)
    {
        std::cout << "[FAIL] Exception: " << e.what() << "\n";
        ++failures;
    }

    if(failures)
    {
        std::cout << failures << " measurement test(s) failed.\n";
        return 1;
    }
    std::cout << "All Ugly Reverb DSP measurements passed.\n";
    return 0;
}
