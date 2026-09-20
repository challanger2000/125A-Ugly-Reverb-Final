#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VST3::Hosting;

namespace {
constexpr int kReloadCycles = 5;

bool runCycle(const fs::path& pluginPath, int cycle)
{
    std::string error;
    auto module = Module::create(pluginPath.u8string(), error);
    if (!module) {
        std::cerr << "[FAIL] cycle " << cycle << ": module load failed: "
                  << (error.empty() ? "unknown" : error) << "\n";
        return false;
    }

    HostApplication hostApplication;
    FUnknown* hostContext = &hostApplication;
    auto factory = module->getFactory();
    factory.setHostContext(hostContext);

    int audioClasses = 0;
    for (const auto& classInfo : factory.classInfos()) {
        if (classInfo.category() != kVstAudioEffectClass &&
            std::string(classInfo.category().data()) != "Audio Mix Processor")
            continue;

        ++audioClasses;
        auto component = factory.createInstance<IComponent>(classInfo.ID());
        if (!component) {
            std::cerr << "[FAIL] cycle " << cycle << ": component creation failed\n";
            return false;
        }

        TUID controllerCID {};
        const bool hasControllerCID =
            component->getControllerClassId(controllerCID) == kResultTrue;

        const auto componentInit = component->initialize(hostContext);
        if (componentInit != kResultOk) {
            std::cerr << "[FAIL] cycle " << cycle
                      << ": component initialize returned " << componentInit << "\n";
            return false;
        }

        IPtr<IEditController> controller;
        if (hasControllerCID) {
            controller = factory.createInstance<IEditController>(VST3::UID(controllerCID));
            if (!controller) {
                component->terminate();
                std::cerr << "[FAIL] cycle " << cycle << ": controller creation failed\n";
                return false;
            }

            const auto controllerInit = controller->initialize(hostContext);
            if (controllerInit != kResultOk) {
                component->terminate();
                std::cerr << "[FAIL] cycle " << cycle
                          << ": controller initialize returned " << controllerInit << "\n";
                return false;
            }

            const auto controllerTerm = controller->terminate();
            if (controllerTerm != kResultOk) {
                component->terminate();
                std::cerr << "[FAIL] cycle " << cycle
                          << ": controller terminate returned " << controllerTerm << "\n";
                return false;
            }
        }

        const auto componentTerm = component->terminate();
        if (componentTerm != kResultOk) {
            std::cerr << "[FAIL] cycle " << cycle
                      << ": component terminate returned " << componentTerm << "\n";
            return false;
        }
    }

    if (audioClasses == 0) {
        std::cerr << "[FAIL] cycle " << cycle << ": no supported audio class\n";
        return false;
    }

    std::cout << "[PASS] cycle " << cycle << "\n";
    return true;
}
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "[FAIL] no VST3 path supplied\n";
        return 2;
    }

    const fs::path pluginPath = fs::u8path(argv[1]);
    for (int cycle = 1; cycle <= kReloadCycles; ++cycle) {
        if (!runCycle(pluginPath, cycle))
            return 1;
    }

    std::cout << "[PASS] five reload lifecycle cycles passed\n";
    return 0;
}
