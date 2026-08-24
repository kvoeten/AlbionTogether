#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <Windows.h>

#include <cstdint>
#include <memory>

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

namespace fable::game::creature::locomotion { class CreatureLocomotionService; }
namespace fable::game::creature::look { class CreatureLookService; }
namespace fable::game::creature::combat { class CreatureCombatService; }
namespace fable::game::creature::animation { class CreatureAnimationService; }
namespace fable::game::hero_pawn::abilities { class HeroWillAbilityService; }
namespace fable::game::player::input { class PlayerInputService; }
namespace fable::game::npc::village { class VillageMembershipService; }
namespace fable::game::npc::simulation { class DummyVillagerService; }
namespace fable::ui { class HudService; }

namespace fable::game
{
    // Owns the native game-facing services used by scripting and multiplayer.
    // This boundary keeps service construction and initialization out of the
    // AngelScript host and gives the same graph a reusable lifetime owner.
    class GameServiceRuntime final
    {
    public:
        GameServiceRuntime();
        ~GameServiceRuntime();

        GameServiceRuntime(const GameServiceRuntime&) = delete;
        GameServiceRuntime& operator=(const GameServiceRuntime&) = delete;

        bool Initialize(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;

        [[nodiscard]] EntityService& Entities() noexcept { return *entityService_; }
        [[nodiscard]] CreatureService& Creatures() noexcept { return *creatureService_; }
        [[nodiscard]] creature::locomotion::CreatureLocomotionService& Locomotion() noexcept { return *creatureLocomotionService_; }
        [[nodiscard]] creature::look::CreatureLookService& Look() noexcept { return *creatureLookService_; }
        [[nodiscard]] creature::combat::CreatureCombatService& Combat() noexcept { return *creatureCombatService_; }
        [[nodiscard]] hero_pawn::abilities::HeroWillAbilityService& HeroWill() noexcept { return *heroWillAbilityService_; }
        [[nodiscard]] creature::animation::CreatureAnimationService& Animation() noexcept { return *creatureAnimationService_; }
        [[nodiscard]] PlayerService& Players() noexcept { return *playerService_; }
        [[nodiscard]] player::input::PlayerInputService& PlayerInput() noexcept { return *playerInputService_; }
        [[nodiscard]] QuestService& Quests() noexcept { return *questService_; }
        [[nodiscard]] NpcService& Npcs() noexcept { return *npcService_; }
        [[nodiscard]] npc::village::VillageMembershipService& Villages() noexcept { return *villageMembershipService_; }
        [[nodiscard]] npc::simulation::DummyVillagerService& DummyVillagers() noexcept { return *dummyVillagerService_; }
        [[nodiscard]] HeroPawnService& HeroPawns() noexcept { return *heroPawnService_; }
        [[nodiscard]] WorldService& World() noexcept { return *worldService_; }
        [[nodiscard]] ui::HudService& Hud() noexcept { return *hudService_; }

    private:
        enum class InitializationStage : std::uint8_t
        {
            None,
            Entities,
            Hud,
            Creatures,
            Locomotion,
            Look,
            Animation,
            Combat,
            HeroWill,
            Villages,
            DummyVillagers,
            PlayerInput,
            Players,
            Npcs,
            HeroPawns,
            Quests,
            World
        };

        bool Complete(
            InitializationStage stage,
            bool initialized) noexcept;
        [[nodiscard]] bool Reached(InitializationStage stage) const noexcept;

        std::unique_ptr<EntityService> entityService_;
        std::unique_ptr<CreatureService> creatureService_;
        std::unique_ptr<creature::locomotion::CreatureLocomotionService> creatureLocomotionService_;
        std::unique_ptr<creature::look::CreatureLookService> creatureLookService_;
        std::unique_ptr<creature::combat::CreatureCombatService> creatureCombatService_;
        std::unique_ptr<hero_pawn::abilities::HeroWillAbilityService> heroWillAbilityService_;
        std::unique_ptr<creature::animation::CreatureAnimationService> creatureAnimationService_;
        std::unique_ptr<PlayerService> playerService_;
        std::unique_ptr<player::input::PlayerInputService> playerInputService_;
        std::unique_ptr<QuestService> questService_;
        std::unique_ptr<NpcService> npcService_;
        std::unique_ptr<npc::village::VillageMembershipService> villageMembershipService_;
        std::unique_ptr<npc::simulation::DummyVillagerService> dummyVillagerService_;
        std::unique_ptr<HeroPawnService> heroPawnService_;
        std::unique_ptr<WorldService> worldService_;
        std::unique_ptr<ui::HudService> hudService_;
        InitializationStage completedStage_ = InitializationStage::None;
    };
}
