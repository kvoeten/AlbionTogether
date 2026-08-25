#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <Windows.h>

#include <memory>

namespace fable::automation::runtime { class RuntimeConfiguration; }
namespace fable::automation::appearance_cycle { class AppearanceCycleScenario; }
namespace fable::game::creature::actions { class CreatureActionLifecycleObserver; }
namespace fable::game::creature::locomotion { class CreatureModeManagerObserver; }
namespace fable::game::creature::ai { class AiBrainUpdateObserver; }
namespace fable::game::entity::presence { class ThingPresenceObserver; }
namespace fable::game::entity::persistence
{
    class SavedEntityMapBlobObserver;
    class ThingSaveProjectionHook;
}
namespace fable::game::npc::population { class PopulationSimulationHook; }
namespace fable::game::world::travel { class WorldTravelObserver; }

namespace fable::game
{
    // Application composition boundary for gameplay-facing runtime systems.
    // Bootstrap owns one instance; scripting, multiplayer, automation, and
    // native services retain independent internal lifecycles.
    class GameplayRuntime final
    {
    public:
        GameplayRuntime();
        ~GameplayRuntime();

        GameplayRuntime(const GameplayRuntime&) = delete;
        GameplayRuntime& operator=(const GameplayRuntime&) = delete;

        bool Initialize(
            HMODULE clientModule,
            HMODULE gameModule,
            const wchar_t* persistentStorageRoot,
            const automation::runtime::RuntimeConfiguration& configuration,
            const core::Diagnostics& diagnostics);
        bool AttachThingPresenceObserver(
            entity::presence::ThingPresenceObserver& observer);
        bool AttachSavedEntityMapBlobObserver(
            entity::persistence::SavedEntityMapBlobObserver& observer);
        bool AttachThingSaveProjectionHook(
            entity::persistence::ThingSaveProjectionHook& hook);
        bool AttachPopulationSimulationHook(
            npc::population::PopulationSimulationHook& hook);
        bool AttachCreatureActionObserver(
            creature::actions::CreatureActionLifecycleObserver& observer);
        bool AttachCreatureModeObserver(
            creature::locomotion::CreatureModeManagerObserver& observer);
        bool AttachAiBrainUpdateObserver(
            creature::ai::AiBrainUpdateObserver& observer);
        bool AttachWorldTravelObserver(
            world::travel::WorldTravelObserver& observer);
        void InitializeAppearanceCycle(
            automation::appearance_cycle::AppearanceCycleScenario& scenario);
        void DispatchKeyPressed(unsigned int virtualKey, bool shiftPressed);
        void DispatchWorldReady();
        bool ProcessMultiplayerPresentation();
        void DriveReplicatedMovement();
        void Tick(float deltaSeconds);
        bool Reload();
        void Shutdown() noexcept;

        [[nodiscard]] bool IsLoaded() const noexcept;

    private:
        class State;
        std::unique_ptr<State> state_;
    };
}
