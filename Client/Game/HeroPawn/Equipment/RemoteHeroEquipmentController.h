#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/HeroPawn/Equipment/HeroEquipmentState.h"
#include "Game/HeroPawn/Equipment/Transitions/RemoteHeroWeaponTransitionController.h"

#include <cstdint>
#include <string>

namespace fable::game
{
    class Entity;
    class EntityService;
}

namespace fable::game::hero_pawn::equipment
{
    // Actor-scoped application of replicated Hero weapon ownership and carry
    // slots. It has no transport knowledge and is reset with the Hero actor.
    class RemoteHeroEquipmentController final
    {
    public:
        bool Initialize(
            game::EntityService& entities,
            const core::Diagnostics& diagnostics) noexcept;
        [[nodiscard]] bool Bind(
            game::Entity& hero,
            void* nativeHero,
            std::uint64_t actorId) noexcept;
        void Reconcile(
            const HeroEquipmentState& state,
            std::uint64_t now);
        [[nodiscard]] bool PrepareWeapon(
            game::creature::equipment::CreatureWeaponFamily family,
            const HeroWeaponDefinitions& requiredWeapons,
            std::uint32_t meleeAttachmentSlot,
            std::uint32_t rangedAttachmentSlot);
        [[nodiscard]] bool PerformTransition(
            const HeroEquipmentState& finalState,
            const std::string& sourceActionType,
            std::uint32_t animationId);
        void Unbind() noexcept;
        void Shutdown() noexcept;

    private:
        game::EntityService* entities_ = nullptr;
        game::Entity* hero_ = nullptr;
        void* nativeHero_ = nullptr;
        std::uint64_t actorId_ = 0;
        HeroEquipmentState applied_ = {};
        HeroEquipmentState attempted_ = {};
        HeroWeaponDefinitions preparedWeapons_ = {};
        HeroWeaponDefinitions actionOverrideWeapons_ = {};
        HeroEquipmentState prunedPresentation_ = {};
        game::creature::equipment::CreatureWeaponFamily activeFamily_ =
            game::creature::equipment::CreatureWeaponFamily::None;
        game::creature::equipment::CreatureWeaponFamily actionOverrideFamily_ =
            game::creature::equipment::CreatureWeaponFamily::None;
        std::uint32_t actionOverrideMeleeSlot_ = 0;
        std::uint32_t actionOverrideRangedSlot_ = 0;
        std::uint64_t nextAttemptAt_ = 0;
        std::uint64_t actionOverrideUntil_ = 0;
        std::uint64_t nextPruneAttemptAt_ = 0;
        std::uint32_t attemptCount_ = 0;
        bool pendingReported_ = false;
        bool usesHeroInventory_ = false;
        bool activeWeaponReady_ = false;
        transitions::RemoteHeroWeaponTransitionController transitions_;
        core::Diagnostics diagnostics_ = {};
    };
}
