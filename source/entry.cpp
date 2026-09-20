#include "processor.h"
#include "controller.h"
#include "ids.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

#define stringPluginName "125A Ugly Reverb"

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)

DEF_CLASS2(INLINE_UID_FROM_FUID(UglyReverb::ProcessorUID),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           stringPluginName,
           Vst::kDistributable,
           "Fx|Reverb",
           FULL_VERSION_STR,
           kVstVersionString,
           UglyReverb::Processor::createInstance)

DEF_CLASS2(INLINE_UID_FROM_FUID(UglyReverb::ControllerUID),
           PClassInfo::kManyInstances,
           kVstComponentControllerClass,
           stringPluginName " Controller",
           0,
           "",
           FULL_VERSION_STR,
           kVstVersionString,
           UglyReverb::Controller::createInstance)

END_FACTORY
