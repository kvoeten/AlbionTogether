#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <Windows.h>

#include <filesystem>
#include <memory>
#include <vector>

class asIScriptEngine;
class asIScriptFunction;

namespace fable::game
{
    class CreatureService;
    class EntityService;
    class HeroPawnService;
    class NpcService;
    class PlayerService;
    class QuestService;
    class WorldService;
}

namespace fable::automation::runtime
{
    class RuntimeConfiguration;
}

namespace fable::automation::local_instance
{
    class MapTransitionAcceptanceDriver;
}

namespace fable::automation::multiplayer::combat
{
    class CombatTargetAcceptanceDriver;
    class CombatVisualExchangeDriver;
}

namespace fable::automation::multiplayer::transition
{
    class NpcTransferAcceptanceDriver;
}

namespace fable::multiplayer
{
    class MultiplayerSession;
}

namespace fable::game::creature::locomotion
{
    class CreatureLocomotionService;
}

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::automation::multiplayer::abilities
{
    class HeroWillAbilityAcceptanceDriver;
}

namespace fable::game::hero_pawn::abilities
{
    class HeroWillAbilityService;
}

namespace fable::game::creature::animation
{
    class CreatureAnimationService;
}

namespace fable::game::creature::look
{
    class CreatureLookService;
}

namespace fable::game::player::input
{
    class PlayerInputService;
}

namespace fable::game::creature::actions
{
    class CreatureActionLifecycleObserver;
}

namespace fable::game::creature::ai
{
    class AiBrainUpdateObserver;
}

namespace fable::game::entity::presence
{
    class ThingPresenceObserver;
}

namespace fable::game::entity::persistence
{
    class SavedEntityMapBlobObserver;
    class ThingSaveProjectionHook;
}

namespace fable::game::npc::population
{
    class PopulationSimulationHook;
}

namespace fable::game::npc::village
{
    class VillageMembershipService;
}

namespace fable::game::npc::simulation
{
    class DummyVillagerService;
}

namespace fable::game::world::travel
{
    class WorldTravelObserver;
}

namespace fable::core
{
    class CapabilityRegistry;
}

namespace fable::ui
{
    class HudService;
}

namespace fable::scripting
{
    class EventBus;
    class PersistentStore;
    class Scheduler;

    class ScriptHost final
    {
    public:
        ScriptHost();
        ~ScriptHost();

        ScriptHost(const ScriptHost&) = delete;
        ScriptHost& operator=(const ScriptHost&) = delete;

        bool Initialize(
            HMODULE clientModule,
            HMODULE gameModule,
            const wchar_t* persistentStorageRoot,
            const automation::runtime::RuntimeConfiguration& runtimeConfiguration,
            const core::Diagnostics& diagnostics);
        bool AttachThingPresenceObserver(
            game::entity::presence::ThingPresenceObserver& observer);
        bool AttachSavedEntityMapBlobObserver(
            game::entity::persistence::SavedEntityMapBlobObserver& observer);
        bool AttachThingSaveProjectionHook(
            game::entity::persistence::ThingSaveProjectionHook& hook);
        bool AttachPopulationSimulationHook(
            game::npc::population::PopulationSimulationHook& hook);
        bool AttachCreatureActionObserver(
            game::creature::actions::CreatureActionLifecycleObserver& observer);
        bool AttachAiBrainUpdateObserver(
            game::creature::ai::AiBrainUpdateObserver& observer);
        bool AttachWorldTravelObserver(
            game::world::travel::WorldTravelObserver& observer);
        void DispatchKeyPressed(unsigned int virtualKey, bool shiftPressed);
        void DispatchWorldReady();
        bool ProcessMultiplayerPresentation();
        void DriveReplicatedMovement();
        void Tick(float deltaSeconds);
        bool Reload();
        void Shutdown();

        [[nodiscard]] bool IsLoaded() const noexcept;

    private:
        struct ScriptModule;

        bool RegisterApi();
        bool LoadModules(const std::filesystem::path& scriptsRoot);
        std::unique_ptr<ScriptModule> BuildModule(
            const std::filesystem::path& scriptPath,
            std::size_t moduleIndex);
        bool Execute(asIScriptFunction* function);
        void ReportExecutionFailure(const char* callbackName, int executionResult);

        asIScriptEngine* engine_ = nullptr;
        std::vector<std::unique_ptr<ScriptModule>> modules_;
        std::unique_ptr<game::EntityService> entityService_;
        std::unique_ptr<game::CreatureService> creatureService_;
        std::unique_ptr<game::creature::locomotion::CreatureLocomotionService>
            creatureLocomotionService_;
        std::unique_ptr<game::creature::look::CreatureLookService>
            creatureLookService_;
        std::unique_ptr<game::creature::combat::CreatureCombatService>
            creatureCombatService_;
        std::unique_ptr<game::hero_pawn::abilities::HeroWillAbilityService>
            heroWillAbilityService_;
        std::unique_ptr<game::creature::animation::CreatureAnimationService>
            creatureAnimationService_;
        std::unique_ptr<game::PlayerService> playerService_;
        std::unique_ptr<game::player::input::PlayerInputService>
            playerInputService_;
        std::unique_ptr<game::QuestService> questService_;
        std::unique_ptr<game::NpcService> npcService_;
        std::unique_ptr<game::npc::village::VillageMembershipService>
            villageMembershipService_;
        std::unique_ptr<game::npc::simulation::DummyVillagerService>
            dummyVillagerService_;
        std::unique_ptr<game::HeroPawnService> heroPawnService_;
        std::unique_ptr<game::WorldService> worldService_;
        std::unique_ptr<ui::HudService> hudService_;
        std::unique_ptr<core::CapabilityRegistry> capabilities_;
        std::unique_ptr<EventBus> events_;
        std::unique_ptr<PersistentStore> storage_;
        std::unique_ptr<Scheduler> scheduler_;
        std::unique_ptr<automation::local_instance::
            MapTransitionAcceptanceDriver> transitionAcceptanceDriver_;
        std::unique_ptr<automation::multiplayer::combat::
            CombatTargetAcceptanceDriver> combatTargetAcceptanceDriver_;
        std::unique_ptr<automation::multiplayer::combat::
            CombatVisualExchangeDriver> combatVisualExchangeDriver_;
        std::unique_ptr<automation::multiplayer::abilities::
            HeroWillAbilityAcceptanceDriver> heroWillAbilityAcceptanceDriver_;
        std::unique_ptr<automation::multiplayer::transition::
            NpcTransferAcceptanceDriver> npcTransferAcceptanceDriver_;
        std::unique_ptr<multiplayer::MultiplayerSession> multiplayerSession_;
        std::filesystem::path scriptsRoot_;
        core::Diagnostics diagnostics_ = {};
        bool heroWillFocusedAcceptance_ = false;
        bool loaded_ = false;
    };
}
