#include "GameServiceRuntime.h"

#include "Game/Creature/Animation/CreatureAnimationService.h"
#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/Creature/CreatureService.h"
#include "Game/Creature/Locomotion/CreatureLocomotionService.h"
#include "Game/Creature/Look/CreatureLookService.h"
#include "Game/Entity/EntityService.h"
#include "Game/HeroPawn/Abilities/HeroWillAbilityService.h"
#include "Game/HeroPawn/HeroPawnService.h"
#include "Game/NPC/NpcService.h"
#include "Game/NPC/Simulation/DummyVillager/DummyVillagerService.h"
#include "Game/NPC/Village/VillageMembershipService.h"
#include "Game/Player/Input/PlayerInputService.h"
#include "Game/Player/PlayerService.h"
#include "Game/Quest/QuestService.h"
#include "UI/Hud/HudService.h"
#include "Game/World/WorldService.h"

namespace fable::game
{
    GameServiceRuntime::GameServiceRuntime()
        : entityService_(std::make_unique<EntityService>()),
          creatureService_(std::make_unique<CreatureService>()),
          creatureLocomotionService_(std::make_unique<creature::locomotion::CreatureLocomotionService>()),
          creatureLookService_(std::make_unique<creature::look::CreatureLookService>()),
          creatureCombatService_(std::make_unique<creature::combat::CreatureCombatService>()),
          heroWillAbilityService_(std::make_unique<hero_pawn::abilities::HeroWillAbilityService>()),
          creatureAnimationService_(std::make_unique<creature::animation::CreatureAnimationService>()),
          playerService_(std::make_unique<PlayerService>()),
          playerInputService_(std::make_unique<player::input::PlayerInputService>()),
          questService_(std::make_unique<QuestService>()),
          npcService_(std::make_unique<NpcService>()),
          villageMembershipService_(std::make_unique<npc::village::VillageMembershipService>()),
          dummyVillagerService_(std::make_unique<npc::simulation::DummyVillagerService>()),
          heroPawnService_(std::make_unique<HeroPawnService>()),
          worldService_(std::make_unique<WorldService>()),
          hudService_(std::make_unique<ui::HudService>())
    {
    }

    GameServiceRuntime::~GameServiceRuntime()
    {
        Shutdown();
    }

    bool GameServiceRuntime::Initialize(HMODULE gameModule, const core::Diagnostics& diagnostics)
    {
        Shutdown();
        if (!Complete(
                InitializationStage::Entities,
                entityService_->Initialize(gameModule, diagnostics)))
        {
            return false;
        }
        if (!Complete(
                InitializationStage::Hud,
                hudService_->Initialize(entityService_->Interface(), diagnostics)))
        {
            return false;
        }
        if (!Complete(InitializationStage::Creatures,
                creatureService_->Initialize(*entityService_, diagnostics)) ||
            !Complete(InitializationStage::Locomotion,
                creatureLocomotionService_->Initialize(*entityService_, diagnostics)) ||
            !Complete(InitializationStage::Look,
                creatureLookService_->Initialize(*entityService_, diagnostics)) ||
            !Complete(InitializationStage::Animation,
                creatureAnimationService_->Initialize(*entityService_, diagnostics)) ||
            !Complete(InitializationStage::Combat,
                creatureCombatService_->Initialize(
                    *entityService_, *creatureAnimationService_, diagnostics)) ||
            !Complete(InitializationStage::HeroWill,
                heroWillAbilityService_->Initialize(*entityService_, diagnostics)) ||
            !Complete(InitializationStage::Villages,
                villageMembershipService_->Initialize(gameModule, diagnostics)) ||
            !Complete(InitializationStage::DummyVillagers,
                dummyVillagerService_->Initialize(gameModule, diagnostics)) ||
            !Complete(InitializationStage::PlayerInput,
                playerInputService_->Initialize(gameModule, diagnostics)) ||
            !Complete(InitializationStage::Players,
                playerService_->Initialize(
                    *entityService_, *creatureService_, diagnostics)) ||
            !Complete(InitializationStage::Npcs,
                npcService_->Initialize(*entityService_, diagnostics)) ||
            !Complete(InitializationStage::HeroPawns,
                heroPawnService_->Initialize(*entityService_, diagnostics)) ||
            !Complete(InitializationStage::Quests,
                questService_->Initialize(*entityService_, diagnostics)) ||
            !Complete(InitializationStage::World,
                worldService_->Initialize(*entityService_, diagnostics)))
        {
            return false;
        }
        return true;
    }

    void GameServiceRuntime::Shutdown() noexcept
    {
        if (Reached(InitializationStage::HeroPawns))
        {
            heroPawnService_->Shutdown();
        }
        if (Reached(InitializationStage::PlayerInput))
        {
            playerInputService_->Shutdown();
        }
        if (Reached(InitializationStage::DummyVillagers))
        {
            dummyVillagerService_->Shutdown();
        }
        if (Reached(InitializationStage::Villages))
        {
            villageMembershipService_->Shutdown();
        }
        if (Reached(InitializationStage::HeroWill))
        {
            heroWillAbilityService_->Shutdown();
        }
        if (Reached(InitializationStage::Combat))
        {
            creatureCombatService_->Shutdown();
        }
        if (Reached(InitializationStage::Animation))
        {
            creatureAnimationService_->Shutdown();
        }
        if (Reached(InitializationStage::Look))
        {
            creatureLookService_->Shutdown();
        }
        if (Reached(InitializationStage::Locomotion))
        {
            creatureLocomotionService_->Shutdown();
        }
        completedStage_ = InitializationStage::None;
    }

    bool GameServiceRuntime::Reached(InitializationStage stage) const noexcept
    {
        return static_cast<std::uint8_t>(completedStage_) >=
            static_cast<std::uint8_t>(stage);
    }

    bool GameServiceRuntime::Complete(
        InitializationStage stage,
        bool initialized) noexcept
    {
        if (!initialized)
        {
            Shutdown();
            return false;
        }
        completedStage_ = stage;
        return true;
    }
}
