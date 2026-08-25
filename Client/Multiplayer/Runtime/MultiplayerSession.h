#pragma once

#include "Multiplayer/Runtime/MultiplayerRuntimeGraph.h"
#include "Multiplayer/Runtime/PresentationLifecycleCoordinator.h"

#include <cstdint>

namespace fable::automation::runtime { class RuntimeConfiguration; }
namespace fable::game { class EntityService; class NpcService; class QuestService; }
namespace fable::game::creature::locomotion { class CreatureLocomotionService; }
namespace fable::game::creature::look { class CreatureLookService; }
namespace fable::game::creature::combat { class CreatureCombatService; }
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

namespace fable::multiplayer
{
    class MultiplayerSession final
    {
    public:
        MultiplayerSession() = default;
        ~MultiplayerSession();
        MultiplayerSession(const MultiplayerSession&) = delete;
        MultiplayerSession& operator=(const MultiplayerSession&) = delete;

        bool Initialize(
            const automation::runtime::RuntimeConfiguration& configuration,
            game::EntityService& entities, game::NpcService& npcs,
            game::creature::locomotion::CreatureLocomotionService& locomotion,
            game::creature::look::CreatureLookService& look,
            game::creature::combat::CreatureCombatService& combat,
            game::hero_pawn::abilities::HeroWillAbilityService& abilities,
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
        void DriveReplicatedMovement();
        void Shutdown() noexcept;
        [[nodiscard]] bool IsEnabled() const noexcept;
        [[nodiscard]] bool IsWorldReady() const noexcept;
        [[nodiscard]] bool HasActiveRemotePresentation() const;
        bool TransferOwnedEntity(std::uint64_t entityUid, std::uint16_t destinationMapId, const game::Vector3& destinationPosition, float destinationFacing);

    private:
        MultiplayerRuntimeGraph graph_;
        PresentationLifecycleCoordinator lifecycle_;
    };
}
