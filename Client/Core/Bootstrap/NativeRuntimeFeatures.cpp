#include "Core/Bootstrap/ClientRuntimeServices.h"
#include "Core/Bootstrap/FeatureRegistry.h"

#include <string>

namespace
{
    using namespace fable::core::bootstrap;
    using fable::automation::runtime::ClientMode;

    fable::core::Diagnostics ScriptDiagnostics() noexcept
    {
        return {ScriptLog, ScriptEvent};
    }

    bool AlwaysEnabled(const FeatureContext&) noexcept { return true; }

    bool InstallEarlyEnvironmentHooks(FeatureContext& context) noexcept
    {
        if (!IsPreResumeStage(context))
        {
            return true;
        }
        auto& core = CoreContext();
        auto& hooks = NativeHooksContext();
        const auto rollback = [&hooks]() noexcept
        {
            hooks.definitions.Shutdown();
            hooks.documents.Shutdown();
            hooks.singleton.Shutdown();
            return false;
        };
        if (core.configuration.IsLocalInstance() &&
            !hooks.singleton.Install(
                core.gameModule,
                core.configuration.LocalSessionId().c_str(),
                core.configuration.LocalInstanceId().c_str()))
        {
            return rollback();
        }
        if (!core.configuration.FixtureDocumentsPath().empty() &&
            !hooks.documents.Install(
                core.gameModule,
                core.configuration.FixtureDocumentsPath().c_str(),
                {}))
        {
            return rollback();
        }
        if (!core.configuration.GameDefinitionsPath().empty() &&
            !hooks.definitions.InstallEarly(
                core.gameModule,
                core.configuration.GameDefinitionsPath().c_str()))
        {
            return rollback();
        }
        return true;
    }

    void UninstallEarlyEnvironmentHooks(FeatureContext& context) noexcept
    {
        if (!IsPreResumeStage(context))
        {
            return;
        }
        auto& hooks = NativeHooksContext();
        hooks.definitions.Shutdown();
        hooks.documents.Shutdown();
        hooks.singleton.Shutdown();
    }

    bool InstallGameplayRuntime(FeatureContext& context) noexcept
    {
        if (IsPreResumeStage(context))
        {
            return true;
        }
        const auto diagnostics = ScriptDiagnostics();
        auto& core = CoreContext();
        if (!GameplayContext().runtime.Initialize(
                core.clientModule,
                core.gameModule,
                core.configuration.ScriptDataPath().c_str(),
                core.configuration,
                diagnostics))
        {
            Log("AngelScript gameplay framework initialization failed.");
            return false;
        }
        if (!core.configuration.FixtureDocumentsPath().empty() &&
            !NativeHooksContext().documents.Install(
                core.gameModule,
                core.configuration.FixtureDocumentsPath().c_str(),
                diagnostics))
        {
            GameplayContext().runtime.Shutdown();
            return false;
        }
        if (!core.configuration.GameDefinitionsPath().empty())
        {
            NativeHooksContext().definitions.Report(diagnostics);
        }
        if (core.configuration.IsLocalInstance())
        {
            NativeHooksContext().singleton.Report(diagnostics);
            const std::wstring localDetail =
                L"session=" + core.configuration.LocalSessionId() +
                L";instance=" + core.configuration.LocalInstanceId();
            LogEvent("LocalInstanceReady", WideToUtf8(localDetail.c_str()).c_str());
        }
        return true;
    }

    void UninstallGameplayRuntime(FeatureContext& context) noexcept
    {
        if (!IsPreResumeStage(context))
        {
            GameplayContext().runtime.Shutdown();
        }
    }

    bool InstallCreatureRuntimeHooks(FeatureContext& context) noexcept
    {
        if (IsPreResumeStage(context))
        {
            return true;
        }
        auto& core = CoreContext();
        auto& hooks = NativeHooksContext();
        auto& gameplay = GameplayContext().runtime;
        const auto diagnostics = ScriptDiagnostics();
        const bool appearance = core.configuration.Mode() == ClientMode::AppearanceCycle ||
            ScenarioIs(L"appearance_cycle");
        const bool multiplayer = core.configuration.MultiplayerEnabled();
        const auto rollback = [&hooks]() noexcept
        {
            hooks.creatureModes.Shutdown();
            hooks.followCreature.Shutdown();
            hooks.physicsNavigator.Shutdown();
            hooks.aiBrain.Shutdown();
            hooks.creatureActions.Shutdown();
            hooks.creatureConstructor.Shutdown();
            return false;
        };

        if (core.configuration.Mode() != ClientMode::TransformProbe &&
            !hooks.creatureConstructor.Install(core.gameModule, diagnostics))
        {
            return rollback();
        }
        if (multiplayer &&
            (!hooks.creatureActions.Install(core.gameModule, diagnostics) ||
             !gameplay.AttachCreatureActionObserver(hooks.creatureActions)))
        {
            return rollback();
        }
        if ((appearance || multiplayer) &&
            (!hooks.aiBrain.Install(core.gameModule, diagnostics) ||
             (multiplayer && !gameplay.AttachAiBrainUpdateObserver(hooks.aiBrain))))
        {
            return rollback();
        }
        if (appearance &&
            (!hooks.physicsNavigator.Install(core.gameModule, diagnostics) ||
             !hooks.followCreature.Install(core.gameModule, diagnostics)))
        {
            return rollback();
        }
        if ((appearance || multiplayer) &&
            !hooks.creatureModes.Install(core.gameModule, diagnostics))
        {
            return rollback();
        }
        if (appearance)
        {
            gameplay.InitializeAppearanceCycle(AutomationContext().appearanceCycle);
        }
        return true;
    }

    void UninstallCreatureRuntimeHooks(FeatureContext& context) noexcept
    {
        if (IsPreResumeStage(context))
        {
            return;
        }
        auto& hooks = NativeHooksContext();
        AutomationContext().appearanceCycle.Shutdown();
        hooks.creatureModes.Shutdown();
        hooks.followCreature.Shutdown();
        hooks.physicsNavigator.Shutdown();
        hooks.aiBrain.Shutdown();
        hooks.creatureActions.Shutdown();
        hooks.creatureConstructor.Shutdown();
    }

    bool InstallEntityWorldRuntimeHooks(FeatureContext& context) noexcept
    {
        if (IsPreResumeStage(context) || !CoreContext().configuration.MultiplayerEnabled())
        {
            return true;
        }
        auto& hooks = NativeHooksContext();
        auto& gameplay = GameplayContext().runtime;
        const auto diagnostics = ScriptDiagnostics();
        HMODULE game = CoreContext().gameModule;
        const bool installed = hooks.thingPresence.Install(game, diagnostics) &&
            gameplay.AttachThingPresenceObserver(hooks.thingPresence) &&
            hooks.savedEntityMap.Install(game, diagnostics) &&
            gameplay.AttachSavedEntityMapBlobObserver(hooks.savedEntityMap) &&
            hooks.worldTravel.Install(game, diagnostics) &&
            gameplay.AttachWorldTravelObserver(hooks.worldTravel) &&
            hooks.thingSave.Install(game, diagnostics) &&
            gameplay.AttachThingSaveProjectionHook(hooks.thingSave) &&
            hooks.population.Install(game, diagnostics) &&
            gameplay.AttachPopulationSimulationHook(hooks.population);
        if (installed)
        {
            return true;
        }
        hooks.population.Shutdown();
        hooks.thingSave.Shutdown();
        hooks.worldTravel.Shutdown();
        hooks.savedEntityMap.Shutdown();
        hooks.thingPresence.Shutdown();
        return false;
    }

    void UninstallEntityWorldRuntimeHooks(FeatureContext& context) noexcept
    {
        if (IsPreResumeStage(context))
        {
            return;
        }
        auto& hooks = NativeHooksContext();
        hooks.population.Shutdown();
        hooks.thingSave.Shutdown();
        hooks.worldTravel.Shutdown();
        hooks.savedEntityMap.Shutdown();
        hooks.thingPresence.Shutdown();
    }

    FABLE_FEATURE_DEPENDENCIES(earlyHookDependencies, "target.validation");
    FABLE_FEATURE_DESCRIPTOR(
        fableEarlyEnvironmentHooksFeature,
        "native.early-environment-hooks",
        "Early environment hooks",
        FeaturePhase::Process,
        20,
        AlwaysEnabled,
        earlyHookDependencies,
        std::size(earlyHookDependencies),
        InstallEarlyEnvironmentHooks,
        UninstallEarlyEnvironmentHooks,
        "early-environment-hook-installation");

    FABLE_FEATURE_DEPENDENCIES(gameplayDependencies, "native.early-environment-hooks");
    FABLE_FEATURE_DESCRIPTOR(
        fableGameplayRuntimeFeature,
        "gameplay.runtime",
        "Gameplay runtime",
        FeaturePhase::Runtime,
        0,
        AlwaysEnabled,
        gameplayDependencies,
        std::size(gameplayDependencies),
        InstallGameplayRuntime,
        UninstallGameplayRuntime,
        "gameplay-runtime-initialization");

    FABLE_FEATURE_DEPENDENCIES(creatureHookDependencies, "gameplay.runtime");
    FABLE_FEATURE_DESCRIPTOR(
        fableCreatureRuntimeHooksFeature,
        "native.creature-hooks",
        "Native creature hooks",
        FeaturePhase::Runtime,
        10,
        AlwaysEnabled,
        creatureHookDependencies,
        std::size(creatureHookDependencies),
        InstallCreatureRuntimeHooks,
        UninstallCreatureRuntimeHooks,
        "native-creature-hook-installation");

    FABLE_FEATURE_DEPENDENCIES(entityWorldDependencies, "native.creature-hooks");
    FABLE_FEATURE_DESCRIPTOR(
        fableEntityWorldRuntimeHooksFeature,
        "native.entity-world-hooks",
        "Native entity and world hooks",
        FeaturePhase::Runtime,
        20,
        AlwaysEnabled,
        entityWorldDependencies,
        std::size(entityWorldDependencies),
        InstallEntityWorldRuntimeHooks,
        UninstallEntityWorldRuntimeHooks,
        "native-entity-world-hook-installation");
}
