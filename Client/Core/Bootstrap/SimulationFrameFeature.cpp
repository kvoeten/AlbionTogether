#include "ClientRuntimeServices.h"
#include "Core/GameThread/Hooks/SimulationFrameHook.h"

namespace
{
    using namespace fable::core::bootstrap;
    using fable::core::game_thread::SimulationFrameHook;

    bool Enabled(const FeatureContext&) noexcept { return true; }
    bool Install(FeatureContext& context) noexcept
    {
        return !IsPreResumeStage(context) ||
            SimulationFrameHook::InstallBeforeResume(CoreContext().gameModule);
    }
    void Detach(FeatureContext&) noexcept { SimulationFrameHook::Disable(); }

    void Dispatch(void* context)
    {
        static_cast<fable::game::GameplayRuntime*>(context)->ProcessSimulationFrame();
    }
    bool Enable(FeatureContext& context) noexcept
    {
        return IsPreResumeStage(context) || SimulationFrameHook::Enable(
            Dispatch, &GameplayContext().runtime, {ScriptLog, ScriptEvent});
    }

    FABLE_FEATURE_DEPENDENCIES(installDependencies, "target.validation");
    FABLE_FEATURE_DESCRIPTOR(
        fableSimulationFrameHookFeature, "native.simulation-frame",
        "Native simulation frame", FeaturePhase::Process, 21, Enabled,
        installDependencies, std::size(installDependencies), Install, Detach,
        "native-simulation-frame-validation");

    // Enable last and detach first: callbacks cannot see partly initialized
    // services, or run while other feature shutdowns release their bindings.
    FABLE_FEATURE_DEPENDENCIES(dispatchDependencies,
        "native.simulation-frame", "ui.game-window", "native.entity-world-hooks");
    FABLE_FEATURE_DESCRIPTOR(
        fableSimulationDispatchFeature, "gameplay.simulation-dispatch",
        "Simulation-owned gameplay", FeaturePhase::Automation, 1000, Enabled,
        dispatchDependencies, std::size(dispatchDependencies), Enable, Detach,
        "native-simulation-dispatch-enable");
}
