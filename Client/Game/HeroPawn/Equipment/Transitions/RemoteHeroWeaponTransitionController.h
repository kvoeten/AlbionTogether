#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/HeroPawn/Equipment/HeroEquipmentState.h"
#include "Multiplayer/Protocol/EquipmentTransitionTiming.h"

#include <cstdint>
#include <string>

namespace fable::game
{
    class Entity;
    class EntityService;
}

namespace fable::game::creature::equipment::native
{
    class CreatureWeaponCache;
}

namespace fable::game::creature::animation
{
    class CreatureAnimationService;
}

namespace fable::game::hero_pawn::equipment::transitions
{
    // Replays one reliable owner-observed draw/stow action on a remote Hero.
    // The body animation starts first; the exact final CTCCarrying slots are
    // applied at the same point at which the retail action mutates them. A
    // newer action/change revision supersedes the pending record immediately.
    class RemoteHeroWeaponTransitionController final
    {
    public:
        bool Initialize(
            game::EntityService& entities,
            game::creature::animation::CreatureAnimationService& animation,
            const core::Diagnostics& diagnostics) noexcept;
        void Bind(
            game::Entity& hero,
            void* nativeHero,
            std::uint64_t actorId,
            game::creature::equipment::native::CreatureWeaponCache&
                weaponCache) noexcept;
        bool Submit(
            const HeroEquipmentState& finalState,
            std::uint32_t animationId,
            std::uint64_t actionId);
        bool Submit(
            const HeroEquipmentState& finalState,
            std::uint32_t animationId,
            std::uint64_t actionId,
            std::uint32_t elapsedMs,
            std::uint32_t durationMs,
            std::uint32_t attachmentNotifyOffsetMs);
        // Compatibility entry point for callers that still carry the
        // originating action name. RepNotify ordering is defined solely by
        // the monotonic action/change revision, never by this string.
        bool Submit(
            const HeroEquipmentState& finalState,
            const std::string& sourceActionType,
            std::uint32_t animationId,
            std::uint64_t actionId);
        void Process(std::uint64_t now) noexcept;
        [[nodiscard]] bool ConsumeAppliedState(
            HeroEquipmentState& state) noexcept;
        void Unbind() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsPending() const noexcept;

    private:
        static constexpr std::uint64_t MutationRetryMilliseconds = 75;

        struct PendingTransition final
        {
            HeroEquipmentState finalState;
            std::uint64_t mutationAt = 0;
            std::uint64_t expiresAt = 0;
            std::uint32_t animationId = 0;
            std::uint64_t actionId = 0;
            std::uint32_t mutationAttempts = 0;
            bool active = false;
        };

        game::EntityService* entities_ = nullptr;
        game::creature::animation::CreatureAnimationService* animation_ =
            nullptr;
        game::creature::equipment::native::CreatureWeaponCache* weaponCache_ =
            nullptr;
        game::Entity* hero_ = nullptr;
        void* nativeHero_ = nullptr;
        std::uint64_t actorId_ = 0;
        PendingTransition pending_;
        HeroEquipmentState appliedState_ = {};
        bool appliedStateAvailable_ = false;
        core::Diagnostics diagnostics_ = {};
        bool initialized_ = false;
        std::uint64_t lastSubmittedActionId_ = 0;
    };
}
