#pragma once

#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Runtime/PresentationLifecycleCoordinator.h"

#include <cstdint>
#include <string>

namespace fable::automation::runtime { class RuntimeConfiguration; }
namespace fable::game { class EntityService; class NpcService; class QuestService; }
namespace fable::game::creature::locomotion { class CreatureLocomotionService; }
namespace fable::game::creature::look { class CreatureLookService; }
namespace fable::game::creature::combat { class CreatureCombatService; }
namespace fable::game::creature::animation { class CreatureAnimationService; }
namespace fable::game::hero_pawn::abilities { class HeroWillAbilityService; }
namespace fable::game::npc::village { class VillageMembershipService; }
namespace fable::game::npc::simulation { class DummyVillagerService; }
namespace fable::game::entity::presence { class ThingPresenceObserver; }
namespace fable::game::entity::persistence { class SavedEntityMapBlobObserver; class ThingSaveProjectionHook; }
namespace fable::game::npc::population { class PopulationSimulationHook; }
namespace fable::game::creature::actions { class CreatureActionLifecycleObserver; }
namespace fable::game::creature::locomotion { class CreatureModeManagerObserver; }
namespace fable::game::creature::ai { class AiBrainUpdateObserver; }
namespace fable::game::world::travel { class WorldTravelObserver; }
namespace fable::ui { class HudService; }

namespace fable::multiplayer
{
    class MultiplayerRuntimeGraph final
    {
    public:
        ~MultiplayerRuntimeGraph();

        bool Initialize(
            const automation::runtime::RuntimeConfiguration& configuration,
            game::EntityService& entities,
            game::NpcService& npcs,
            game::creature::locomotion::CreatureLocomotionService& locomotion,
            game::creature::look::CreatureLookService& look,
            game::creature::animation::CreatureAnimationService& animation,
            game::creature::combat::CreatureCombatService& combat,
            game::hero_pawn::abilities::HeroWillAbilityService& abilities,
            ui::HudService& hud,
            game::QuestService& quests,
            game::npc::village::VillageMembershipService& villages,
            game::npc::simulation::DummyVillagerService& dummyVillagers,
            const core::Diagnostics& diagnostics);
        bool AttachThingPresenceObserver(game::entity::presence::ThingPresenceObserver& observer);
        bool AttachSavedEntityMapBlobObserver(game::entity::persistence::SavedEntityMapBlobObserver& observer);
        bool AttachThingSaveProjectionHook(game::entity::persistence::ThingSaveProjectionHook& hook);
        bool AttachPopulationSimulationHook(game::npc::population::PopulationSimulationHook& hook);
        bool AttachCreatureActionObserver(game::creature::actions::CreatureActionLifecycleObserver& observer);
        bool AttachCreatureModeObserver(game::creature::locomotion::CreatureModeManagerObserver& observer);
        bool AttachAiBrainUpdateObserver(game::creature::ai::AiBrainUpdateObserver& observer);
        bool AttachWorldTravelObserver(game::world::travel::WorldTravelObserver& observer);
        bool OnWorldReady();
        bool ProcessPresentationLifecycle();
        bool ProcessPlayerActorState();
        void DriveReplicatedMovement();
        void Shutdown() noexcept;

        [[nodiscard]] bool IsEnabled() const noexcept { return enabled_; }
        [[nodiscard]] bool IsWorldReady() const noexcept { return enabled_ && contexts_.players.localHero.IsWorldReady(); }
        [[nodiscard]] bool HasActiveRemotePresentation() const;
        bool TransferOwnedEntity(std::uint64_t uid, std::uint16_t mapId, const game::Vector3& position, float facing);

        bool ReconcileEntityLifecycle(const std::string& mapName, std::uint16_t mapId, bool publishLocalChanges = true, std::uint16_t ignoredDepartingMapId = 0);
        [[nodiscard]] bool IsOwnerRosterReady(const std::string& mapName) const noexcept;
        [[nodiscard]] core::Diagnostics& Diagnostics() noexcept { return diagnostics_; }
        [[nodiscard]] MultiplayerSessionContexts& Contexts() noexcept { return contexts_; }
        [[nodiscard]] const MultiplayerSessionContexts& Contexts() const noexcept { return contexts_; }

    private:
        enum class InitializationStage : std::uint32_t
        {
            None = 0,
            Players = 1u << 0,
            Transport = 1u << 1,
            Components = 1u << 2,
            ReliableDispatcher = 1u << 3,
            ConstructionGate = 1u << 4,
        };

        void MarkStage(InitializationStage stage) noexcept;
        [[nodiscard]] bool HasStage(InitializationStage stage) const noexcept;
        MultiplayerSessionContexts contexts_;
        PresentationLifecycleCoordinator lifecycle_;
        core::Diagnostics diagnostics_ = {};
        std::uint32_t initializedStages_ = 0;
        bool enabled_ = false;
    };
}
