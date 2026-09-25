#include "processor.h"
#include "parameters.h"
#include "state_format.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"

#include <algorithm>
#include <cmath>
#include <chrono>
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

double decayCrossingSeconds(const std::vector<float>& x,double sr,double fraction)
{
    if(x.empty() || sr<=0.0) return 0.0;
    const size_t win=std::max<size_t>(1,(size_t)std::llround(sr*0.05));
    double peak=0.0;
    std::vector<double> powers;
    for(size_t pos=0;pos<x.size();pos+=win)
    {
        const size_t end=std::min(x.size(),pos+win);
        const double p=meanSquare(x,pos,end);
        powers.push_back(p);
        peak=std::max(peak,p);
    }
    if(peak<=1e-30) return 0.0;
    const double target=peak*fraction;
    for(size_t i=1;i<powers.size();++i)
    {
        bool staysBelow=true;
        for(size_t j=i;j<powers.size();++j)
            if(powers[j]>target) { staysBelow=false; break; }
        if(staysBelow) return (double)(i*win)/sr;
    }
    return (double)x.size()/sr;
}

double hfProxy(const std::vector<float>& x, size_t start)
{
    start = std::min(start, x.size());
    if (x.size() < 2 || start >= x.size() - 1) return 0.0;
    double diffEnergy = 0.0;
    double signalEnergy = 0.0;
    for (size_t i = std::max<size_t>(start + 1, 1); i < x.size(); ++i)
    {
        const double v = x[i];
        const double d = v - x[i - 1];
        diffEnergy += d * d;
        signalEnergy += v * v;
    }
    return signalEnergy > 1e-24 ? diffEnergy / signalEnergy : 0.0;
}

double normalizedCorrelation(const std::vector<float>& a,const std::vector<float>& b,
                             size_t start,size_t end)
{
    end=std::min({end,a.size(),b.size()});
    start=std::min(start,end);
    double ab=0.0,aa=0.0,bb=0.0;
    for(size_t i=start;i<end;++i)
    {
        const double x=a[i];
        const double y=b[i];
        ab+=x*y;
        aa+=x*x;
        bb+=y*y;
    }
    const double denom=std::sqrt(std::max(aa*bb,1e-30));
    return denom>0.0?ab/denom:0.0;
}

double crestFactor(const std::vector<float>& x,size_t start,size_t end)
{
    end=std::min(end,x.size());
    start=std::min(start,end);
    if(end<=start) return 0.0;
    double peak=0.0;
    double sumSq=0.0;
    for(size_t i=start;i<end;++i)
    {
        const double a=std::fabs((double)x[i]);
        peak=std::max(peak,a);
        sumSq+=(double)x[i]*(double)x[i];
    }
    const double rms=std::sqrt(sumSq/(double)(end-start));
    return rms>1e-20?peak/rms:0.0;
}

double activeSampleFraction(const std::vector<float>& x,size_t start,size_t end,double relativeThreshold)
{
    end=std::min(end,x.size());
    start=std::min(start,end);
    if(end<=start) return 0.0;
    double rms=0.0;
    for(size_t i=start;i<end;++i) rms+=(double)x[i]*(double)x[i];
    rms=std::sqrt(rms/(double)(end-start));
    const double threshold=rms*relativeThreshold;
    size_t active=0;
    for(size_t i=start;i<end;++i)
        if(std::fabs((double)x[i])>=threshold) ++active;
    return (double)active/(double)(end-start);
}

size_t firstAbove(const std::vector<float>& x,double threshold)
{
    for(size_t i=0;i<x.size();++i)
        if(std::fabs((double)x[i])>threshold) return i;
    return x.size();
}

RenderResult render(double sr, double seconds, float material, float preDelay, float digital,
                    bool bypass=false, bool impulse=true, int block=128, float decay=0.58f,
                    float metal=0.68f, float clang=0.55f, float damping=0.48f,
                    float rattle=0.12f, float diffusion=0.45f, float body=0.55f,
                    float mix=1.f, float width=0.75f, float impulseAmplitude=1.f,
                    Steinberg::Vst::ProcessModes processMode=kRealtime, float size=0.55f,
                    float output=0.5f)
{
    block = std::max(1, block);
    Processor p;
    if (p.initialize(nullptr) != kResultOk)
        throw std::runtime_error("Processor initialize failed");

    ProcessSetup setup {};
    setup.processMode = processMode;
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
    p.setTestParameter(UglyReverb::kSize, size);
    p.setTestParameter(UglyReverb::kWidth, width);
    p.setTestParameter(UglyReverb::kMix, bypass ? 0.28f : mix);
    p.setTestParameter(UglyReverb::kOutput, output);
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
            inL[0]=impulseAmplitude;
            inR[0]=impulseAmplitude;
            sent=true;
        }

        ProcessData data {};
        data.processMode=processMode;
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


RenderResult renderProgramFixture(double sr,double seconds,float material,float damping,
                                  float diffusion,float metal,float clang,float mix=1.f,
                                  int block=128,float rattle=0.18f)
{
    block=std::max(1,block);
    Processor p;
    if(p.initialize(nullptr)!=kResultOk)
        throw std::runtime_error("Processor initialize failed");

    ProcessSetup setup {};
    setup.processMode=kRealtime;
    setup.symbolicSampleSize=kSample32;
    setup.maxSamplesPerBlock=block;
    setup.sampleRate=sr;
    if(p.setupProcessing(setup)!=kResultOk)
        throw std::runtime_error("setupProcessing failed");

    p.setTestParameter(UglyReverb::kMaterial,material);
    p.setTestParameter(UglyReverb::kPreDelay,0.04f);
    p.setTestParameter(UglyReverb::kDecay,0.68f);
    p.setTestParameter(UglyReverb::kDamping,damping);
    p.setTestParameter(UglyReverb::kDiffusion,diffusion);
    p.setTestParameter(UglyReverb::kMetal,metal);
    p.setTestParameter(UglyReverb::kClang,clang);
    p.setTestParameter(UglyReverb::kRattle,rattle);
    p.setTestParameter(UglyReverb::kBody,0.58f);
    p.setTestParameter(UglyReverb::kWidth,0.78f);
    p.setTestParameter(UglyReverb::kMix,mix);
    p.setTestParameter(UglyReverb::kOutput,0.5f);
    p.setTestParameter(UglyReverb::kDigital,0.f);
    p.setTestParameter(UglyReverb::kBypass,0.f);

    if(p.setActive(true)!=kResultOk)
        throw std::runtime_error("setActive failed");

    const size_t total=(size_t)std::llround(sr*seconds);
    RenderResult rr;
    rr.left.assign(total,0.f);
    rr.right.assign(total,0.f);

    std::vector<float> inL(block,0.f),inR(block,0.f),outL(block,0.f),outR(block,0.f);
    float* inPtrs[2]={inL.data(),inR.data()};
    float* outPtrs[2]={outL.data(),outR.data()};
    AudioBusBuffers inBus {}; inBus.numChannels=2; inBus.channelBuffers32=inPtrs;
    AudioBusBuffers outBus {}; outBus.numChannels=2; outBus.channelBuffers32=outPtrs;

    size_t pos=0;
    while(pos<total)
    {
        const int n=(int)std::min<size_t>(block,total-pos);
        std::fill(inL.begin(),inL.end(),0.f);
        std::fill(inR.begin(),inR.end(),0.f);
        std::fill(outL.begin(),outL.end(),0.f);
        std::fill(outR.begin(),outR.end(),0.f);

        for(int i=0;i<n;++i)
        {
            const size_t sample=pos+(size_t)i;
            const double t=(double)sample/sr;

            // Deterministic pseudo-program: kick-like low pulse every 0.5 s,
            // brighter short transient every 0.25 s, plus a low-level two-tone
            // sustain.  This exercises repeated excitation and spectral memory
            // without relying on external copyrighted audio fixtures.
            const double halfBeat=std::fmod(t,0.5);
            const double quarterBeat=std::fmod(t,0.25);
            const float lowPulse=(halfBeat<0.035)
                ? (float)(0.75*std::exp(-halfBeat*80.0)*std::sin(2.0*3.14159265358979323846*62.0*t))
                : 0.f;
            const float click=(quarterBeat<0.010)
                ? (float)(0.28*std::exp(-quarterBeat*260.0)*std::sin(2.0*3.14159265358979323846*2300.0*t))
                : 0.f;
            const float sustain=(float)(0.045*std::sin(2.0*3.14159265358979323846*220.0*t)
                                      +0.030*std::sin(2.0*3.14159265358979323846*880.0*t));
            inL[(size_t)i]=lowPulse+click+sustain;
            inR[(size_t)i]=lowPulse+0.82f*click+0.97f*sustain;
        }

        ProcessData data {};
        data.processMode=kRealtime;
        data.symbolicSampleSize=kSample32;
        data.numSamples=n;
        data.numInputs=1;
        data.numOutputs=1;
        data.inputs=&inBus;
        data.outputs=&outBus;
        if(p.process(data)!=kResultOk)
            throw std::runtime_error("program fixture process failed");

        for(int i=0;i<n;++i)
        {
            rr.left[pos+(size_t)i]=outL[(size_t)i];
            rr.right[pos+(size_t)i]=outR[(size_t)i];
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

        auto pre0=render(48000.0,0.40,0.5f,0.f,0.f);
        auto pre50=render(48000.0,0.40,0.5f,0.5f,0.f);
        auto pre100=render(48000.0,0.45,0.5f,1.f,0.f);
        const double onset0Ms=1000.0*(double)firstAbove(pre0.left,1e-8)/48000.0;
        const double onset50Ms=1000.0*(double)firstAbove(pre50.left,1e-8)/48000.0;
        const double onset100Ms=1000.0*(double)firstAbove(pre100.left,1e-8)/48000.0;
        const double shift50Ms=onset50Ms-onset0Ms;
        const double shift100Ms=onset100Ms-onset0Ms;
        std::cout << "[INFO] v2_predelay_onset_ms="
                  << onset0Ms << "," << onset50Ms << "," << onset100Ms
                  << " shifts=" << shift50Ms << "," << shift100Ms << "\n";
        require(onset0Ms < 20.0,
                "V2 PreDelay 0% starts the wet response promptly", failures);
        require(shift50Ms > 88.0 && shift50Ms < 92.0,
                "V2 PreDelay 50% adds about 90 ms", failures);
        require(shift100Ms > 178.0 && shift100Ms < 182.0,
                "V2 PreDelay 100% adds about 180 ms", failures);

        const float materialValues[] = {0.f,0.1f,0.2f,0.3f,0.4f,0.5f,0.6f,0.7f,0.8f,0.9f,1.f};
        const char* materialNames[] = {"Plate","Thin Plate","Heavy Plate","Sheet","Spring","Steel",
                                       "Pipe","Metal Drum","Oil Can","Chamber","Tank"};
        std::vector<RenderResult> materials;
        for (float m : materialValues) materials.push_back(render(48000.0,1.5,m,0.f,0.f));
        double minAdjacentMaterialDelta=1.0e9;
        double maxAdjacentMaterialDelta=0.0;
        for (int i=0;i<10;++i)
        {
            const double d=difference(materials[i].left,materials[i+1].left);
            minAdjacentMaterialDelta=std::min(minAdjacentMaterialDelta,d);
            maxAdjacentMaterialDelta=std::max(maxAdjacentMaterialDelta,d);
            std::cout << "[INFO] material_delta_" << materialNames[i]
                      << "_to_" << materialNames[i+1] << "=" << d << "\n";
            require(d > 1e-5,
                    std::string(materialNames[i])+" differs from "+materialNames[i+1], failures);
        }
        std::cout << "[INFO] material_delta_min=" << minAdjacentMaterialDelta
                  << " max=" << maxAdjacentMaterialDelta << "\n";

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

        auto sizeMin=render(48000.0,2.0,0.5f,0.f,0.f,false,true,128,
                            0.58f,0.68f,0.55f,0.48f,0.12f,0.45f,0.55f,
                            1.f,0.75f,1.f,kRealtime,0.f);
        auto sizeMax=render(48000.0,2.0,0.5f,0.f,0.f,false,true,128,
                            0.58f,0.68f,0.55f,0.48f,0.12f,0.45f,0.55f,
                            1.f,0.75f,1.f,kRealtime,1.f);
        const double sizeDelta=difference(sizeMin.left,sizeMax.left);
        std::cout << "[INFO] v2_size_delta=" << sizeDelta << "\n";
        require(sizeDelta > 1e-4,
                "V2 Size materially changes the reverb structure across its range", failures);

        auto bodyMin=render(48000.0,2.0,0.5f,0.f,0.f,false,true,128,
                            0.58f,0.68f,0.55f,0.48f,0.12f,0.45f,0.f);
        auto bodyMax=render(48000.0,2.0,0.5f,0.f,0.f,false,true,128,
                            0.58f,0.68f,0.55f,0.48f,0.12f,0.45f,1.f);
        const double bodyDelta=difference(bodyMin.left,bodyMax.left);
        std::cout << "[INFO] v2_body_delta=" << bodyDelta << "\n";
        require(bodyDelta > 1e-4,
                "V2 Body materially changes the modal balance across its range", failures);

        auto decay0=render(48000.0,3.0,0.5f,0.f,0.f,false,true,128,0.f);
        auto decay25=render(48000.0,3.0,0.5f,0.f,0.f,false,true,128,0.25f);
        auto decay50=render(48000.0,3.0,0.5f,0.f,0.f,false,true,128,0.50f);
        auto decay75=render(48000.0,3.0,0.5f,0.f,0.f,false,true,128,0.75f);
        auto decay100=render(48000.0,3.0,0.5f,0.f,0.f,false,true,128,1.f);
        const size_t decayLateStart=(size_t)(48000.0*1.25);
        const double d0=energy(decay0.left,decayLateStart,decay0.left.size());
        const double d25=energy(decay25.left,decayLateStart,decay25.left.size());
        const double d50=energy(decay50.left,decayLateStart,decay50.left.size());
        const double d75=energy(decay75.left,decayLateStart,decay75.left.size());
        const double d100=energy(decay100.left,decayLateStart,decay100.left.size());
        const double t25=decayCrossingSeconds(decay25.left,48000.0,1.0e-3);
        const double t50=decayCrossingSeconds(decay50.left,48000.0,1.0e-3);
        const double t75=decayCrossingSeconds(decay75.left,48000.0,1.0e-3);
        const double t100=decayCrossingSeconds(decay100.left,48000.0,1.0e-3);
        std::cout << "[INFO] v2_decay_ladder="
                  << d0 << "," << d25 << "," << d50 << "," << d75 << "," << d100
                  << " crossing_s=" << t25 << "," << t50 << "," << t75 << "," << t100 << "\n";
        require(t50>t25 && t75>t50 && t100>=t75,
                "V2 Decay increases measured tail duration through 25/50/75/100%", failures);
        require(d25>d0 && d50>d25 && d75>d50 && d100>d75,
                "V2 Decay late-tail energy rises monotonically 0/25/50/75/100%", failures);
        require(d50>d25*1.5 && d75>d50*1.5,
                "V2 Decay has useful separation through the musical range", failures);

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

        const size_t densityStart=(size_t)(48000.0*0.20);
        const size_t densityEnd=(size_t)(48000.0*1.20);
        const double lowCrest=crestFactor(lowScatter.left,densityStart,densityEnd);
        const double highCrest=crestFactor(highScatter.left,densityStart,densityEnd);
        const double lowActive=activeSampleFraction(lowScatter.left,densityStart,densityEnd,0.35);
        const double highActive=activeSampleFraction(highScatter.left,densityStart,densityEnd,0.35);
        std::cout << "[INFO] v2_diffusion_density_low_crest=" << lowCrest
                  << " high_crest=" << highCrest
                  << " low_active=" << lowActive
                  << " high_active=" << highActive << "\n";
        require(std::isfinite(lowCrest) && std::isfinite(highCrest)
                && std::isfinite(lowActive) && std::isfinite(highActive),
                "V2 diffusion density metrics remain finite", failures);
        require(highCrest < lowCrest * 0.75,
                "V2 high Diffusion measurably lowers tail crest factor", failures);
        require(highActive > lowActive * 1.15,
                "V2 high Diffusion increases active-sample echo density", failures);

        auto scatterBlock1=render(48000.0,1.5,0.5f,0.f,0.f,false,true,1,
                                  0.72f,0.62f,0.42f,0.52f,0.08f,0.85f,0.55f);
        auto scatterBlock512=render(48000.0,1.5,0.5f,0.f,0.f,false,true,512,
                                    0.72f,0.62f,0.42f,0.52f,0.08f,0.85f,0.55f);
        require(difference(scatterBlock1.left,scatterBlock512.left) < 1e-7,
                "V2 scattering render is block-size deterministic", failures);

        // V2 frequency-shaped damping: the same tank should lose substantially
        // more HF structure at maximum Damping without becoming numerically unstable.
        auto openDamping=render(48000.0,2.5,0.5f,0.f,0.f,false,true,128,
                                0.72f,0.74f,0.42f,0.0f,0.08f,0.72f,0.55f);
        auto closedDamping=render(48000.0,2.5,0.5f,0.f,0.f,false,true,128,
                                  0.72f,0.74f,0.42f,1.0f,0.08f,0.72f,0.55f);
        const size_t dampingStart=(size_t)(48000.0*0.20);
        const double openHF=hfProxy(openDamping.left,dampingStart);
        const double closedHF=hfProxy(closedDamping.left,dampingStart);
        const double openTailEnergy=energy(openDamping.left,dampingStart,openDamping.left.size());
        const double closedTailEnergy=energy(closedDamping.left,dampingStart,closedDamping.left.size());
        std::cout << "[INFO] v2_damping_hf_open=" << openHF
                  << " closed=" << closedHF
                  << " ratio=" << (closedHF > 0.0 ? openHF/closedHF : 0.0)
                  << " tail_energy_ratio="
                  << (openTailEnergy > 0.0 ? closedTailEnergy/openTailEnergy : 0.0) << "\n";
        require(openHF > closedHF * 1.20,
                "V2 Damping clearly reduces normalized high-frequency tail structure", failures);
        require(closedTailEnergy > openTailEnergy * 0.01,
                "V2 maximum Damping preserves a meaningful reverb tail", failures);
        require(finiteBuffer(openDamping.left) && finiteBuffer(closedDamping.left),
                "V2 frequency-shaped damping remains finite", failures);

        // V2 sparse early-reflection excitation must stay deterministic.  The
        // topology intentionally uses fixed material fingerprints, never RNG.
        auto earlyA=render(48000.0,1.2,0.3f,0.f,0.f,false,true,128,
                           0.55f,0.58f,0.35f,0.42f,0.06f,0.92f,0.48f);
        auto earlyB=render(48000.0,1.2,0.3f,0.f,0.f,false,true,128,
                           0.55f,0.58f,0.35f,0.42f,0.06f,0.92f,0.48f);
        require(difference(earlyA.left,earlyB.left) < 1e-8,
                "V2 sparse early-reflection excitation is deterministic", failures);

        auto earlyLow=render(48000.0,0.45,0.3f,0.f,0.f,false,true,128,
                             0.42f,0.50f,0.28f,0.40f,0.02f,0.0f,0.50f);
        auto earlyHigh=render(48000.0,0.45,0.3f,0.f,0.f,false,true,128,
                              0.42f,0.50f,0.28f,0.40f,0.02f,1.0f,0.50f);
        const size_t earlyStart=(size_t)(48000.0*0.015);
        const size_t earlyEnd=(size_t)(48000.0*0.18);
        const double earlyDelta=difference(earlyLow.left,earlyHigh.left);
        const double earlyLowEnergy=energy(earlyLow.left,earlyStart,earlyEnd);
        const double earlyHighEnergy=energy(earlyHigh.left,earlyStart,earlyEnd);
        std::cout << "[INFO] v2_early_delta=" << earlyDelta
                  << " low_energy=" << earlyLowEnergy
                  << " high_energy=" << earlyHighEnergy << "\n";
        require(earlyDelta > 1e-4,
                "V2 sparse onset cloud materially changes early response", failures);

        // WIDTH must now alter the internal tank relationship, not merely scale
        // the output side channel.  Compare wet tails with identical mono input.
        auto narrowTank=render(48000.0,1.8,0.5f,0.f,0.f,false,true,128,
                               0.62f,0.60f,0.40f,0.46f,0.05f,0.70f,0.50f,1.0f,0.0f);
        auto wideTank=render(48000.0,1.8,0.5f,0.f,0.f,false,true,128,
                             0.62f,0.60f,0.40f,0.46f,0.05f,0.70f,0.50f,1.0f,1.0f);
        const size_t corrStart=(size_t)(48000*0.20);
        const size_t corrEnd=(size_t)(48000*1.50);
        const double narrowCorr=std::fabs(normalizedCorrelation(
            narrowTank.left,narrowTank.right,corrStart,corrEnd));
        const double wideCorr=std::fabs(normalizedCorrelation(
            wideTank.left,wideTank.right,corrStart,corrEnd));
        const double widthDelta=difference(narrowTank.left,wideTank.left);
        std::cout << "[INFO] v2_width_corr_narrow=" << narrowCorr
                  << " wide=" << wideCorr
                  << " delta=" << widthDelta << "\n";
        require(std::isfinite(narrowCorr) && std::isfinite(wideCorr),
                "V2 stereo tank correlation metrics remain finite", failures);
        require(wideCorr < narrowCorr * 0.90,
                "V2 Width 100% decorrelates the wet tank versus Width 0%", failures);
        require(widthDelta > 1e-4,
                "V2 Width materially changes the internal tank response", failures);

        auto realtimeRender=render(48000.0,1.5,0.7f,0.12f,0.5f,false,true,127,
                                   0.73f,0.82f,0.61f,0.44f,0.19f,0.77f,0.58f,
                                   1.0f,0.82f,1.f,kRealtime);
        auto offlineRender=render(48000.0,1.5,0.7f,0.12f,0.5f,false,true,127,
                                  0.73f,0.82f,0.61f,0.44f,0.19f,0.77f,0.58f,
                                  1.0f,0.82f,1.f,kOffline);
        const double offlineDelta=difference(realtimeRender.left,offlineRender.left);
        std::cout << "[INFO] v2_offline_realtime_delta=" << offlineDelta << "\n";
        require(offlineDelta < 1e-7,
                "V2 offline and realtime renders are deterministic matches", failures);

        // Subnormal hardening: an input far below the zap threshold must not
        // seed a persistent feedback tail.
        auto subnormalProbe=render(48000.0,0.8,0.5f,0.f,0.f,false,true,128,
                                   0.95f,0.8f,0.7f,0.3f,0.1f,0.8f,0.55f,
                                   1.0f,0.75f,1.0e-25f);
        const double subnormalEnergy=energy(
            subnormalProbe.left,0,subnormalProbe.left.size())
            + energy(subnormalProbe.right,0,subnormalProbe.right.size());
        std::cout << "[INFO] v2_subnormal_probe_energy=" << subnormalEnergy << "\n";
        require(subnormalEnergy == 0.0,
                "V2 subnormal input cannot seed a feedback tail", failures);

        // MIX semantics are strict: 100% means zero dry contribution and 0%
        // means exact dry.  A non-zero pre-delay makes any leaked direct signal
        // at sample zero immediately detectable.
        auto wetPurity=render(48000.0,0.25,0.5f,0.5f,0.f,false,true,128,
                              0.58f,0.68f,0.55f,0.48f,0.12f,0.45f,0.55f,
                              1.0f,0.75f);
        require(wetPurity.left[0] == 0.f && wetPurity.right[0] == 0.f,
                "Mix 100% is pure wet with no dry leakage", failures);
        auto dryPurity=render(48000.0,0.05,0.5f,0.5f,0.f,false,true,128,
                              0.58f,0.68f,0.55f,0.48f,0.12f,0.45f,0.55f,
                              0.0f,0.75f);
        require(dryPurity.left[0] == 1.f && dryPurity.right[0] == 1.f,
                "Mix 0% is exact dry at unity output", failures);

        auto outMinus12=render(48000.0,0.05,0.5f,0.f,0.f,false,true,128,
                                   0.58f,0.68f,0.55f,0.48f,0.12f,0.45f,0.55f,
                                   0.f,0.75f,1.f,kRealtime,0.55f,0.f);
        auto outZero=render(48000.0,0.05,0.5f,0.f,0.f,false,true,128,
                            0.58f,0.68f,0.55f,0.48f,0.12f,0.45f,0.55f,
                            0.f,0.75f,1.f,kRealtime,0.55f,0.5f);
        auto outPlus12=render(48000.0,0.05,0.5f,0.f,0.f,false,true,128,
                              0.58f,0.68f,0.55f,0.48f,0.12f,0.45f,0.55f,
                              0.f,0.75f,1.f,kRealtime,0.55f,1.f);
        const double expectedMinus12=std::pow(10.0,-12.0/20.0);
        const double expectedPlus12=std::pow(10.0,12.0/20.0);
        std::cout << "[INFO] v2_output_gain_samples="
                  << outMinus12.left[0] << "," << outZero.left[0] << "," << outPlus12.left[0] << "\n";
        require(std::fabs((double)outMinus12.left[0]-expectedMinus12)<1e-6,
                "Output -12 dB maps accurately", failures);
        require(std::fabs((double)outZero.left[0]-1.0)<1e-7,
                "Output 0 dB is unity", failures);
        require(std::fabs((double)outPlus12.left[0]-expectedPlus12)<1e-5,
                "Output +12 dB maps accurately", failures);

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
        require(p.getTailSamples()==14400000u, "Reported reverb tail is 300 seconds at 48 kHz", failures);

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

        // Malformed positive bus counts with null bus arrays must fail cleanly,
        // never dereference null host data.
        {
            Processor malformed;
            malformed.initialize(nullptr);
            ProcessSetup setup {};
            setup.processMode=kRealtime;
            setup.symbolicSampleSize=kSample32;
            setup.maxSamplesPerBlock=64;
            setup.sampleRate=48000.0;
            malformed.setupProcessing(setup);
            malformed.setActive(true);

            ProcessData bad {};
            bad.processMode=kRealtime;
            bad.symbolicSampleSize=kSample32;
            bad.numSamples=64;
            bad.numInputs=1;
            bad.numOutputs=1;
            bad.inputs=nullptr;
            bad.outputs=nullptr;
            require(malformed.process(bad)==kResultFalse,
                    "Malformed null bus arrays fail cleanly", failures);

            malformed.setActive(false);
            malformed.terminate();
        }

        // An inactive VST3 input bus may expose null channel sample pointers.
        // The reverb must treat this as silence and continue an existing tail
        // into an active output bus without crashing.
        {
            Processor inactiveInput;
            inactiveInput.initialize(nullptr);
            ProcessSetup setup {};
            setup.processMode=kRealtime;
            setup.symbolicSampleSize=kSample32;
            setup.maxSamplesPerBlock=128;
            setup.sampleRate=48000.0;
            inactiveInput.setupProcessing(setup);
            inactiveInput.setTestParameter(UglyReverb::kMix,1.f);
            inactiveInput.setTestParameter(UglyReverb::kPreDelay,0.f);
            inactiveInput.setActive(true);

            float inL[128] {}, inR[128] {}, outL[128] {}, outR[128] {};
            inL[0]=1.f; inR[0]=1.f;
            float* inPtrs[2]={inL,inR};
            float* outPtrs[2]={outL,outR};
            AudioBusBuffers inBus {}; inBus.numChannels=2; inBus.channelBuffers32=inPtrs;
            AudioBusBuffers outBus {}; outBus.numChannels=2; outBus.channelBuffers32=outPtrs;
            ProcessData data {};
            data.processMode=kRealtime; data.symbolicSampleSize=kSample32; data.numSamples=128;
            data.numInputs=1; data.numOutputs=1; data.inputs=&inBus; data.outputs=&outBus;
            require(inactiveInput.process(data)==kResultOk,
                    "V2 active input excitation processes successfully", failures);

            float** nullInputChannels=nullptr;
            inBus.channelBuffers32=nullInputChannels;
            std::fill(std::begin(outL),std::end(outL),0.f);
            std::fill(std::begin(outR),std::end(outR),0.f);
            require(inactiveInput.process(data)==kResultOk,
                    "V2 inactive input bus with null samples processes safely", failures);
            double inactiveTailEnergy=0.0;
            for(float v:outL) inactiveTailEnergy+=(double)v*(double)v;
            for(float v:outR) inactiveTailEnergy+=(double)v*(double)v;
            require(inactiveTailEnergy>0.0,
                    "V2 inactive input bus still advances an existing reverb tail", failures);

            inactiveInput.setActive(false);
            inactiveInput.terminate();
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

        // Repeated-excitation pseudo-program fixture.  This complements impulse
        // responses by exercising spectral memory and overlapping reverb tails.
        auto programDefault=renderProgramFixture(48000.0,4.0,0.5f,0.48f,0.45f,0.68f,0.55f);
        auto programDark=renderProgramFixture(48000.0,4.0,0.5f,1.0f,0.45f,0.68f,0.55f);
        auto programDiffuse=renderProgramFixture(48000.0,4.0,0.5f,0.48f,1.0f,0.68f,0.55f);
        require(finiteBuffer(programDefault.left) && finiteBuffer(programDefault.right)
                && finiteBuffer(programDark.left) && finiteBuffer(programDiffuse.left),
                "V2 repeated-excitation program fixture remains finite", failures);
        const double programDampingDelta=difference(programDefault.left,programDark.left);
        const double programDiffusionDelta=difference(programDefault.left,programDiffuse.left);
        const double programDefaultHF=hfProxy(programDefault.left,(size_t)(48000.0*0.5));
        const double programDarkHF=hfProxy(programDark.left,(size_t)(48000.0*0.5));
        std::cout << "[INFO] v2_program_damping_delta=" << programDampingDelta
                  << " diffusion_delta=" << programDiffusionDelta
                  << " default_hf=" << programDefaultHF
                  << " dark_hf=" << programDarkHF << "\n";
        require(programDampingDelta > 1e-4,
                "V2 Damping materially affects repeated-excitation program material", failures);
        require(programDiffusionDelta > 1e-4,
                "V2 Diffusion materially affects repeated-excitation program material", failures);
        require(programDefaultHF > programDarkHF * 1.10,
                "V2 Damping darkens repeated-excitation program material", failures);

        auto rattleOff=renderProgramFixture(48000.0,4.0,0.8f,0.42f,0.52f,0.82f,0.62f,
                                            1.f,128,0.f);
        auto rattleFullA=renderProgramFixture(48000.0,4.0,0.8f,0.42f,0.52f,0.82f,0.62f,
                                              1.f,128,1.f);
        auto rattleFullB=renderProgramFixture(48000.0,4.0,0.8f,0.42f,0.52f,0.82f,0.62f,
                                              1.f,128,1.f);
        const double rattleProgramDelta=difference(rattleOff.left,rattleFullA.left);
        const double rattleRepeatDelta=difference(rattleFullA.left,rattleFullB.left);
        std::cout << "[INFO] v2_rattle_program_delta=" << rattleProgramDelta
                  << " repeat_delta=" << rattleRepeatDelta << "\n";
        require(rattleProgramDelta > 1e-4,
                "V2 Rattle 100% materially changes repeated-excitation material", failures);
        require(rattleRepeatDelta < 1e-8,
                "V2 Rattle modulation remains deterministic", failures);
        require(finiteBuffer(rattleFullA.left) && finiteBuffer(rattleFullA.right),
                "V2 Rattle 100% remains finite on repeated excitation", failures);

        // CI timing is informational only because hosted-runner CPU allocation is
        // not deterministic.  Tracking the same render over time still exposes
        // large V2 performance regressions without turning noisy timing into a gate.
        const auto perfStart=std::chrono::steady_clock::now();
        auto perfProbe=render(96000.0,10.0,0.6f,0.f,1.f,false,true,128,
                              0.85f,0.85f,0.75f,0.25f,0.55f,0.85f,0.60f,
                              1.0f,1.0f);
        const auto perfEnd=std::chrono::steady_clock::now();
        const double perfSeconds=std::chrono::duration<double>(perfEnd-perfStart).count();
        const double realtimeFactor=perfSeconds>0.0?10.0/perfSeconds:0.0;
        std::cout << "[INFO] v2_perf_render_seconds=" << perfSeconds
                  << " realtime_factor=" << realtimeFactor << "\n";
        require(finiteBuffer(perfProbe.left) && finiteBuffer(perfProbe.right),
                "V2 performance probe remains finite", failures);

        auto extremeBlock1=render(48000.0,2.0,0.6f,1.f,1.f,false,true,1,
                                  1.f,1.f,1.f,0.f,1.f,1.f,1.f,
                                  1.f,1.f,1.f,kRealtime,1.f);
        auto extremeBlock512=render(48000.0,2.0,0.6f,1.f,1.f,false,true,512,
                                    1.f,1.f,1.f,0.f,1.f,1.f,1.f,
                                    1.f,1.f,1.f,kRealtime,1.f);
        const double extremeBlockDelta=difference(extremeBlock1.left,extremeBlock512.left);
        std::cout << "[INFO] v2_extreme_block_delta=" << extremeBlockDelta << "\n";
        require(extremeBlockDelta < 1e-7,
                "V2 extreme moving-delay render is block-size deterministic", failures);

        for (float material : {0.f,0.1f,0.2f,0.3f,0.4f,0.5f,0.6f,0.7f,0.8f,0.9f,1.f})
        {
            auto extreme = render(96000.0, 6.0, material, 1.f, 1.f, false, true, 128,
                                  1.f, 1.f, 1.f, 0.f, 1.f, 1.f, 1.f,
                                  1.f, 1.f, 1.f, kRealtime, 1.f);
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
