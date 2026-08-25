#include "GameplayRuntime.h"

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
    class GameplayRuntime::State final
    {
    public:
        GameServiceRuntime services;
        multiplayer::MultiplayerSession multiplayer;
        automation::runtime::AutomationRunner automation;
        scripting::ScriptHost scripts;
        core::Diagnostics diagnostics = {};
        bool servicesReady = false;
        bool multiplayerReady = false;
        bool automationReady = false;
        bool scriptsReady = false;
    };

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
                state_->services.Combat(),
                state_->services.HeroWill(),
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

        if (!state_->scripts.Initialize(
                clientModule,
                persistentStorageRoot,
                state_->services,
                diagnostics))
        {
            Shutdown();
            return false;
        }
        state_->scriptsReady = true;
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
        if (!state_->services.Combat().AttachActionLifecycleObserver(observer))
        {
            return false;
        }
        if (!state_->services.HeroWill().AttachActionLifecycleObserver(observer))
        {
            state_->services.Combat().DetachActionLifecycleObserver();
            return false;
        }
        if (state_->multiplayer.AttachCreatureActionObserver(observer))
        {
            return true;
        }
        state_->services.HeroWill().DetachActionLifecycleObserver();
        state_->services.Combat().DetachActionLifecycleObserver();
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

    void GameplayRuntime::DispatchKeyPressed(
        unsigned int virtualKey,
        bool shiftPressed)
    {
        state_->scripts.DispatchKeyPressed(virtualKey, shiftPressed);
    }

    void GameplayRuntime::DispatchWorldReady()
    {
        if (!state_->multiplayerReady || !state_->multiplayer.OnWorldReady())
        {
            state_->diagnostics.Event("ClientFailed", "multiplayer-world-entry");
            return;
        }
        state_->scripts.DispatchWorldReady();
    }

    bool GameplayRuntime::ProcessMultiplayerPresentation()
    {
        return state_->scriptsReady && state_->multiplayerReady &&
            state_->multiplayer.ProcessPresentationLifecycle();
    }

    void GameplayRuntime::DriveReplicatedMovement()
    {
        if (state_->scriptsReady && state_->multiplayerReady)
        {
            state_->multiplayer.DriveReplicatedMovement();
        }
    }

    void GameplayRuntime::Tick(float deltaSeconds)
    {
        if (!state_->scriptsReady)
        {
            return;
        }
        state_->services.Locomotion().TickHeroShadow();
        state_->automation.Tick(
            deltaSeconds,
            state_->multiplayer.HasActiveRemotePresentation());
        state_->scripts.Tick(deltaSeconds);
    }

    bool GameplayRuntime::Reload()
    {
        if (state_->multiplayerReady && state_->multiplayer.IsEnabled())
        {
            state_->diagnostics.Event(
                "ScriptReloadSkipped",
                "multiplayer owns shared locomotion and combat routes");
            return false;
        }
        if (state_->servicesReady)
        {
            state_->services.Combat().ClearPlayerCombat();
            state_->services.Locomotion().ClearHeroShadow();
        }
        return state_->scripts.Reload();
    }

    void GameplayRuntime::Shutdown() noexcept
    {
        if (state_ == nullptr)
        {
            return;
        }

        // Multiplayer owns the native service callbacks it attached. Its
        // shutdown clears those sinks before transport and actor state retire.
        state_->automation.Shutdown();
        state_->automationReady = false;
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
        return state_ != nullptr && state_->scriptsReady &&
            state_->scripts.IsLoaded();
    }
}
