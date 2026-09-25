#include "controller.h"
#include "parameters.h"
#include "state_format.h"
#include "base/source/fstreamer.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "base/source/fstring.h"
#include "UglyControls.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace UglyReverb {
namespace {
constexpr int32 kGuiStateMagic = 0x315A4755; // "UGZ1" in little-endian storage
constexpr int32 kGuiStateVersion = 1;
constexpr double kGuiZoomFactors[] {1.0, 1.25, 1.5, 1.75, 2.0};
}

tresult PLUGIN_API Controller::initialize(FUnknown* context)
{
    auto r = EditController::initialize(context);
    if (r != kResultOk) return r;

    auto* material = new StringListParameter(STR16("Material"), kMaterial);
    material->appendString(STR16("Plate"));
    material->appendString(STR16("Thin Plate"));
    material->appendString(STR16("Heavy Plate"));
    material->appendString(STR16("Sheet"));
    material->appendString(STR16("Spring"));
    material->appendString(STR16("Steel"));
    material->appendString(STR16("Pipe"));
    material->appendString(STR16("Metal Drum"));
    material->appendString(STR16("Oil Can"));
    material->appendString(STR16("Chamber"));
    material->appendString(STR16("Tank"));
    parameters.addParameter(material);

    auto addRange=[&](const TChar* title,ParamID id,const TChar* unit,
                       ParamValue minValue,ParamValue maxValue,ParamValue defaultValue,
                       int32 precision) {
        auto* p=new RangeParameter(title,id,unit,minValue,maxValue,defaultValue);
        p->setPrecision(precision);
        parameters.addParameter(p);
    };
    addRange(STR16("Size"),kSize,STR16("%"),0.0,100.0,55.0,0);
    addRange(STR16("Decay"),kDecay,STR16("%"),0.0,100.0,58.0,0);
    addRange(STR16("Pre-Delay"),kPreDelay,STR16("ms"),0.0,180.0,14.4,1);
    addRange(STR16("Diffusion"),kDiffusion,STR16("%"),0.0,100.0,45.0,0);
    addRange(STR16("Damping"),kDamping,STR16("%"),0.0,100.0,48.0,0);
    addRange(STR16("Metal"),kMetal,STR16("%"),0.0,100.0,68.0,0);
    addRange(STR16("Clang"),kClang,STR16("%"),0.0,100.0,55.0,0);
    addRange(STR16("Rattle"),kRattle,STR16("%"),0.0,100.0,12.0,0);
    addRange(STR16("Body"),kBody,STR16("%"),0.0,100.0,55.0,0);
    addRange(STR16("Width"),kWidth,STR16("%"),0.0,100.0,75.0,0);
    addRange(STR16("Mix"),kMix,STR16("%"),0.0,100.0,28.0,0);
    addRange(STR16("Output"),kOutput,STR16("dB"),-12.0,12.0,0.0,1);

    auto* digital = new StringListParameter(STR16("Digital Color"), kDigital);
    digital->appendString(STR16("Clean"));
    digital->appendString(STR16("12-bit"));
    digital->appendString(STR16("8-bit"));
    parameters.addParameter(digital);

    auto* bypass = new StringListParameter(STR16("Bypass"), kBypass, nullptr,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass | ParameterInfo::kIsList);
    bypass->appendString(STR16("Off"));
    bypass->appendString(STR16("On"));
    parameters.addParameter(bypass);
    return kResultOk;
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state)
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
    {
        const double normalized = std::isfinite(values[i])
            ? std::clamp<double>(values[i], 0.0, 1.0)
            : 0.0;
        setParamNormalized(ids[i], normalized);
    }
    setParamNormalized(kBypass, bp ? 1.0 : 0.0);
    return kResultOk;
}

tresult PLUGIN_API Controller::setState(IBStream* state)
{
    if (!state) return kResultFalse;

    IBStreamer s(state, kLittleEndian);
    int32 magic = 0;
    if (!s.readInt32(magic)) {
        guiZoomIndex_ = 0;
        if (openEditor_) openEditor_->setZoomFactor(kGuiZoomFactors[0]);
        return kResultOk; // pre-zoom projects had no controller-private state
    }
    if (magic != kGuiStateMagic) {
        guiZoomIndex_ = 0;
        if (openEditor_) openEditor_->setZoomFactor(kGuiZoomFactors[0]);
        return kResultOk;
    }

    int32 version = 0;
    int32 zoomIndex = 0;
    if (!s.readInt32(version) || version != kGuiStateVersion)
        return kResultOk;
    if (!s.readInt32(zoomIndex))
        return kResultOk;

    guiZoomIndex_ = std::clamp(static_cast<int>(zoomIndex), 0, 4);
    if (openEditor_)
        openEditor_->setZoomFactor(kGuiZoomFactors[guiZoomIndex_]);
    return kResultOk;
}

tresult PLUGIN_API Controller::getState(IBStream* state)
{
    if (!state) return kResultFalse;

    IBStreamer s(state, kLittleEndian);
    if (!s.writeInt32(kGuiStateMagic)) return kResultFalse;
    if (!s.writeInt32(kGuiStateVersion)) return kResultFalse;
    if (!s.writeInt32(static_cast<int32>(std::clamp(guiZoomIndex_, 0, 4)))) return kResultFalse;
    return kResultOk;
}

Steinberg::IPlugView* PLUGIN_API Controller::createView(const char* name)
{
    if (!name)
        return nullptr;
    Steinberg::ConstString viewName(name);
    if (viewName == Steinberg::Vst::ViewType::kEditor)
        {
        auto* editor = new VSTGUI::VST3Editor(this, "view", "ugly_reverb.uidesc");
        editor->setAllowedZoomFactors({1.0, 1.25, 1.5, 1.75, 2.0});
        editor->setZoomFactor(kGuiZoomFactors[std::clamp(guiZoomIndex_, 0, 4)]);
        return editor;
    }
    return nullptr;
}

void Controller::didOpen(VSTGUI::VST3Editor* editor)
{
    openEditor_ = editor;
    if (openEditor_)
        openEditor_->setZoomFactor(kGuiZoomFactors[std::clamp(guiZoomIndex_, 0, 4)]);
}

void Controller::willClose(VSTGUI::VST3Editor* editor)
{
    if (openEditor_ == editor)
        openEditor_ = nullptr;
}

VSTGUI::CView* Controller::createCustomView(VSTGUI::UTF8StringPtr name,
    const VSTGUI::UIAttributes& a,const VSTGUI::IUIDescription* d,VSTGUI::VST3Editor* e)
{
    if(!name||!e||!d) return nullptr;
    VSTGUI::CPoint o{0,0},s{60,60}; a.getPointAttribute("origin",o); a.getPointAttribute("size",s);
    VSTGUI::CRect r(o.x,o.y,o.x+s.x,o.y+s.y);
    if(std::strcmp(name,"Faceplate")==0) return new UglyFaceplate(r);
    if(std::strcmp(name,"WearOverlay")==0) return new UglyTextureOverlay(r,"ugly_wear_overlay.png",1.f);
    if(std::strcmp(name,"GlassOverlay")==0) return new UglyTextureOverlay(r,"ugly_glass_overlay.png",1.f);
    if(std::strcmp(name,"BrandLogo")==0) return new UglyLogo(r);
    if(std::strcmp(name,"GuiZoom")==0) return new UglyZoomControl(r,e,&guiZoomIndex_);
    auto defaultFor=[&](ParamID id)->float {
        if(auto* p=parameters.getParameter(id))
            return static_cast<float>(p->getInfo().defaultNormalizedValue);
        return 0.f;
    };
    auto knob=[&](const char* n,ParamID id)->VSTGUI::CView*{
        return std::strcmp(name,n)==0?new UglyKnob(r,e,id,defaultFor(id)):nullptr;
    };
    if(auto*v=knob("Material",kMaterial))return v; if(auto*v=knob("Size",kSize))return v;
    if(auto*v=knob("Decay",kDecay))return v; if(auto*v=knob("PreDelay",kPreDelay))return v;
    if(auto*v=knob("Diffusion",kDiffusion))return v; if(auto*v=knob("Damping",kDamping))return v;
    if(auto*v=knob("Body",kBody))return v; if(auto*v=knob("Metal",kMetal))return v;
    if(auto*v=knob("Clang",kClang))return v; if(auto*v=knob("Rattle",kRattle))return v;
    if(auto*v=knob("Width",kWidth))return v;
    if(std::strcmp(name,"Digital")==0)
        return new UglySelector(r,e,kDigital,std::vector<std::string>{"CLEAN","12 BIT","8 BIT"},defaultFor(kDigital));
    if(auto*v=knob("Mix",kMix))return v; if(auto*v=knob("Output",kOutput))return v;
    if(std::strcmp(name,"Bypass")==0) return new UglyToggle(r,e,kBypass,defaultFor(kBypass));
    return nullptr;
}

} // namespace UglyReverb
