#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/HeroPawn/Equipment/HeroEquipmentState.h"
#include "Game/HeroPawn/Equipment/Hooks/RemoteRangedWeaponOrientationHook.h"
#include "Game/HeroPawn/Equipment/Transitions/RemoteHeroWeaponTransitionController.h"
#include "Game/Creature/Equipment/Native/CreatureWeaponCache.h"
#include "Game/Creature/Equipment/Native/CreatureWeaponFunctions.h"

#include <cstdint>
#include <string>

namespace fable::game
{
    class Entity;
    class EntityService;
}

namespace fable::game::creature::animation
{
    class CreatureAnimationService;
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
            game::creature::animation::CreatureAnimationService& animation,
            hooks::RemoteRangedWeaponOrientationHook& orientationHook,
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
            std::uint32_t animationId,
            std::uint64_t actionId);
        [[nodiscard]] bool PerformTransition(
            const HeroEquipmentState& finalState,
            std::uint32_t animationId,
            std::uint64_t actionId,
            std::uint32_t elapsedMs,
            std::uint32_t durationMs,
            std::uint32_t attachmentNotifyOffsetMs);
        // Explicit native readiness probe. A sane network baseline is not
        // enough: the promoted Hero must expose the requested carry state.
        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] bool IsTransitionPending() const noexcept;
        void Unbind() noexcept;
        void Shutdown() noexcept;

    private:
        [[nodiscard]] bool WarmWeaponCache(
            const HeroEquipmentState& state,
            std::uint64_t now);
        void TrackRangedOrientation(
            game::creature::equipment::CreatureWeaponFamily family,
            void* rangedWeapon,
            std::int32_t rangedWeaponType,
            std::int32_t rangedDefinitionIndex) noexcept;

        game::EntityService* entities_ = nullptr;
        hooks::RemoteRangedWeaponOrientationHook* orientationHook_ = nullptr;
        game::Entity* hero_ = nullptr;
        void* nativeHero_ = nullptr;
        std::uint64_t actorId_ = 0;
        HeroEquipmentState applied_ = {};
        HeroEquipmentState attempted_ = {};
        HeroWeaponDefinitions preparedWeapons_ = {};
        HeroWeaponDefinitions cachedWeapons_ = {};
        HeroWeaponDefinitions actionOverrideWeapons_ = {};
        HeroEquipmentState prunedPresentation_ = {};
        game::creature::equipment::CreatureWeaponFamily activeFamily_ =
            game::creature::equipment::CreatureWeaponFamily::None;
        game::creature::equipment::CreatureWeaponFamily actionOverrideFamily_ =
            game::creature::equipment::CreatureWeaponFamily::None;
        std::uint32_t actionOverrideMeleeSlot_ = 0;
        std::uint32_t actionOverrideRangedSlot_ = 0;
        std::uint64_t nextAttemptAt_ = 0;
        std::uint64_t nextCacheWarmAt_ = 0;
        std::uint64_t actionOverrideUntil_ = 0;
        std::uint64_t nextPruneAttemptAt_ = 0;
        std::uint64_t lastTransitionActionId_ = 0;
        std::uint32_t attemptCount_ = 0;
        hooks::RemoteRangedWeaponOrientationHook::RegistrationToken
            orientationToken_ = 0;
        bool pendingReported_ = false;
        bool activeWeaponReady_ = false;
        game::creature::equipment::native::CreatureWeaponCache weaponCache_;
        transitions::RemoteHeroWeaponTransitionController transitions_;
        core::Diagnostics diagnostics_ = {};
    };
}
