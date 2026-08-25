#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/HeroPawn/Appearance/Hooks/RemoteHeroPresentationFactoryHook.h"
#include "Game/HeroPawn/Equipment/Hooks/RemoteRangedWeaponOrientationHook.h"
#include "Game/HeroPawn/Remote/RemoteHeroActor.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace fable::game
{
    class Entity;
    class EntityService;
    class NpcService;
}

namespace fable::game::creature::locomotion
{
    class CreatureLocomotionService;
}

namespace fable::game::creature::look
{
    class CreatureLookService;
}

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::game::hero_pawn::abilities
{
    class HeroWillAbilityService;
}

namespace fable::multiplayer::combat
{
    class PlayerCombatantDirectory;
}

namespace fable::multiplayer::presentation
{
    // Dynamically owns one native presentation per live remote actor ID. The
    // shared factory hook is process-global; individual actor state is not.
    class RemotePlayerRegistry final
    {
    public:
        bool Initialize(
            game::EntityService& entities,
            game::NpcService& npcs,
            game::creature::locomotion::CreatureLocomotionService& locomotion,
            game::creature::look::CreatureLookService& look,
            game::creature::combat::CreatureCombatService& combat,
            game::hero_pawn::abilities::HeroWillAbilityService& abilities,
            multiplayer::combat::PlayerCombatantDirectory& combatants,
            const core::Diagnostics& diagnostics,
            std::uint64_t localActorId);
        void Reconcile(
            const std::vector<replication::RemotePlayerSnapshot>& snapshots,
            const std::string& localMap,
            game::Entity* localHero);
        void Remove(std::uint64_t actorId) noexcept;
        void BeginWorldTransition() noexcept;
        void CompleteWorldTransition() noexcept;
        void DriveMovement();
        bool ApplyHealth(
            std::uint64_t actorId,
            float currentHealth,
            float maximumHealth,
            std::uint32_t revision);
        bool PerformAbility(
            std::uint64_t actorId,
            game::creature::equipment::CreatureWeaponFamily weaponFamily,
            const game::hero_pawn::equipment::HeroWeaponDefinitions&
                requiredWeapons,
            std::uint32_t meleeAttachmentSlot,
            std::uint32_t rangedAttachmentSlot,
            std::uint32_t abilityId,
            float charge,
            void* targetCreature,
            const std::string& resolvedActionType,
            std::uint32_t resolvedAnimationId);
        bool EndRangedAim(std::uint64_t actorId) noexcept;
        bool PerformWeaponTransition(
            std::uint64_t actorId,
            game::creature::equipment::CreatureWeaponFamily weaponFamily,
            const game::hero_pawn::equipment::HeroWeaponDefinitions&
                requiredWeapons,
            std::uint32_t meleeAttachmentSlot,
            std::uint32_t rangedAttachmentSlot,
            const std::string& resolvedActionType,
            std::uint32_t resolvedAnimationId);
        bool PerformHeroAbility(
            std::uint64_t actorId,
            game::hero_pawn::abilities::HeroAbility ability,
            game::hero_pawn::abilities::HeroAbilityCommand command,
            std::int32_t progressionState,
            void* targetCreature);
        void Shutdown() noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] std::size_t ActiveCount() const;
        [[nodiscard]] bool IsLifecycleActive(
            std::uint64_t actorId,
            std::uint32_t actorGeneration,
            std::uint32_t mapEpoch) const noexcept;

    private:
        std::unique_ptr<game::hero_pawn::remote::RemoteHeroActor>
            CreatePresentation();

        game::EntityService* entities_ = nullptr;
        game::NpcService* npcs_ = nullptr;
        game::creature::locomotion::CreatureLocomotionService* locomotion_ =
            nullptr;
        game::creature::look::CreatureLookService* look_ = nullptr;
        game::creature::combat::CreatureCombatService* combat_ = nullptr;
        game::hero_pawn::abilities::HeroWillAbilityService* abilities_ =
            nullptr;
        multiplayer::combat::PlayerCombatantDirectory* combatants_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        game::hero_pawn::appearance::hooks::RemoteHeroPresentationFactoryHook
            presentationFactory_;
        game::hero_pawn::equipment::hooks::
            RemoteRangedWeaponOrientationHook rangedOrientation_;
        std::unordered_map<
            std::uint64_t,
            std::unique_ptr<game::hero_pawn::remote::RemoteHeroActor>>
                presentations_;
        std::uint64_t localActorId_ = 0;
        bool initialized_ = false;
    };
}
