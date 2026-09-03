#include "GameplayRuntime.h"
#include "GameplayRuntimeState.h"
#include "Core/GameThread/Hooks/SimulationFrameHook.h"

#include "Game/Creature/Animation/CreatureAnimationService.h"
#include "DeveloperTools/Game/MultiplayerSaveSectionStatusProvider.h"
#include "UI/ImGui/Runtime/ImGuiDx9Runtime.h"

#include "Automation/Runtime/AutomationRunner.h"
#include "Automation/AppearanceCycle/AppearanceCycleScenario.h"
#include "Automation/Runtime/RuntimeConfiguration.h"
#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/Creature/Locomotion/CreatureLocomotionService.h"
#include "Game/HeroPawn/Abilities/HeroWillAbilityService.h"
#include "Game/Runtime/GameServiceRuntime.h"
#include "Multiplayer/Runtime/MultiplayerSession.h"
#include "Scripting/Runtime/Host/ScriptHost.h"

namespace fable::game
{
    GameplayRuntime::GameplayRuntime()
        : state_(std::make_unique<State>())
    {
    }

    GameplayRuntime::~GameplayRuntime()
    {
        Shutdown();
    }

    bool GameplayRuntime::Initialize(
        HMODULE clientModule,
        HMODULE gameModule,
        const wchar_t* persistentStorageRoot,
        const automation::runtime::RuntimeConfiguration& configuration,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        state_->diagnostics = diagnostics;
        if (!state_->services.Initialize(gameModule, diagnostics))
        {
            diagnostics.Log("AngelScript: creature or player service initialization failed.");
            Shutdown();
            return false;
        }
        state_->servicesReady = true;
        if (!state_->multiplayer.Initialize(
                configuration,
                state_->services.Entities(),
                state_->services.Npcs(),
                state_->services.Locomotion(),
                state_->services.Look(),
                state_->services.Animation(),
                state_->services.Combat(),
                state_->services.HeroWill(),
                state_->services.Hud(),
                state_->services.Quests(),
                state_->services.Villages(),
                state_->services.DummyVillagers(),
                diagnostics))
        {
            diagnostics.Log("Multiplayer: session initialization failed.");
            Shutdown();
            return false;
        }
        state_->multiplayerReady = true;

        if (!state_->automation.Initialize(
                configuration,
                state_->services,
                state_->multiplayer,
                diagnostics))
        {
            diagnostics.Log("Automation: runner initialization failed.");
            Shutdown();
            return false;
        }
        state_->automationReady = true;

        state_->developerSaveSections.Bind(&state_->multiplayer);
        if (!state_->developerTools.Initialize(
                state_->services.Entities(),
                state_->services.Npcs(),
                state_->services.Quests(),
                &state_->developerSaveSections,
                !state_->multiplayer.IsEnabled() ||
                    configuration.MultiplayerRole() == L"host",
                state_->multiplayer.IsEnabled()
                    ? state_->multiplayer.Contexts().transport.transport.
                        ConnectionNonce()
                    : 0))
        {
            diagnostics.Log("Developer tools: native API initialization failed.");
            Shutdown();
            return false;
        }

        if (!state_->scripts.Initialize(
                clientModule,
                persistentStorageRoot,
                state_->services,
                diagnostics))
        {
            Shutdown();
            return false;
        }
        if (!state_->scriptUi.Install(gameModule, state_->scripts, diagnostics))
        {
            diagnostics.Log("AngelScript ImGui framework initialization failed.");
            Shutdown();
            return false;
        }
        state_->scriptsReady.store(true, std::memory_order_release);
        return true;
    }

    bool GameplayRuntime::AttachThingPresenceObserver(
        entity::presence::ThingPresenceObserver& observer)
    {
        return state_->multiplayerReady &&
            state_->multiplayer.AttachThingPresenceObserver(observer);
    }

    bool GameplayRuntime::AttachSavedEntityMapBlobObserver(
        entity::persistence::SavedEntityMapBlobObserver& observer)
    {
        return state_->multiplayerReady &&
            state_->multiplayer.AttachSavedEntityMapBlobObserver(observer);
    }

    bool GameplayRuntime::AttachThingSaveProjectionHook(
        entity::persistence::ThingSaveProjectionHook& hook)
    {
        return state_->multiplayerReady &&
            state_->multiplayer.AttachThingSaveProjectionHook(hook);
    }

    bool GameplayRuntime::AttachPopulationSimulationHook(
        npc::population::PopulationSimulationHook& hook)
    {
        return state_->multiplayerReady &&
            state_->multiplayer.AttachPopulationSimulationHook(hook);
    }

    bool GameplayRuntime::AttachCreatureActionObserver(
        creature::actions::CreatureActionLifecycleObserver& observer)
    {
        if (!state_->servicesReady || !state_->multiplayerReady)
        {
            return false;
        }
        if (!state_->services.Animation().AttachActionLifecycleObserver(
                observer))
        {
            return false;
        }
        if (!state_->services.HeroWill().AttachActionLifecycleObserver(observer))
        {
            state_->services.Animation().DetachActionLifecycleObserver();
            return false;
        }
        if (state_->multiplayer.AttachCreatureActionObserver(observer))
        {
            return true;
        }
        state_->services.HeroWill().DetachActionLifecycleObserver();
        state_->services.Animation().DetachActionLifecycleObserver();
        return false;
    }

    bool GameplayRuntime::AttachCreatureModeObserver(
        creature::locomotion::CreatureModeManagerObserver& observer)
    {
        return state_->multiplayerReady &&
            state_->multiplayer.AttachCreatureModeObserver(observer);
    }

    bool GameplayRuntime::AttachAiBrainUpdateObserver(
        creature::ai::AiBrainUpdateObserver& observer)
    {
        return state_->multiplayerReady &&
            state_->multiplayer.AttachAiBrainUpdateObserver(observer);
    }

    bool GameplayRuntime::AttachWorldTravelObserver(
        world::travel::WorldTravelObserver& observer)
    {
        return state_->multiplayerReady &&
            state_->multiplayer.AttachWorldTravelObserver(observer);
    }

    void GameplayRuntime::InitializeAppearanceCycle(
        automation::appearance_cycle::AppearanceCycleScenario& scenario)
    {
        scenario.Initialize(state_->scripts, state_->diagnostics);
    }

    void GameplayRuntime::ToggleDeveloperTools(HWND owner) noexcept
    {
        (void)owner;
        state_->scriptUi.Toggle();
    }

    void GameplayRuntime::CloseDeveloperTools() noexcept
    {
        state_->scriptUi.Hide();
    }

    bool GameplayRuntime::HandleDeveloperToolsWindowMessage(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam) noexcept
    {
        return state_ != nullptr && state_->scriptUi.HandleWindowMessage(
            window, message, wParam, lParam);
    }

    bool GameplayRuntime::IsDeveloperToolsAvailable() const noexcept
    {
        return state_ != nullptr && state_->scriptUi.IsAvailable();
    }

    void GameplayRuntime::Shutdown() noexcept
    {
        if (state_ == nullptr)
        {
            return;
        }

        core::game_thread::SimulationFrameHook::Disable();
        state_->scriptsReady.store(false, std::memory_order_release);
        state_->mailbox.Reset();
        state_->lastFrameAt = 0;

        // Multiplayer owns the native service callbacks it attached. Its
        // shutdown clears those sinks before transport and actor state retire.
        state_->automation.Shutdown();
        state_->automationReady = false;
        (void)state_->scriptUi.Shutdown();
        state_->developerTools.Shutdown();
        state_->developerSaveSections.Bind(nullptr);
        state_->multiplayer.Shutdown();
        state_->multiplayerReady = false;
        state_->scripts.Shutdown();
        state_->scriptsReady = false;
        state_->services.Shutdown();
        state_->servicesReady = false;
        state_->diagnostics = {};
    }

    bool GameplayRuntime::IsLoaded() const noexcept
    {
        return state_ != nullptr &&
            state_->scriptsReady.load(std::memory_order_acquire);
    }
}
