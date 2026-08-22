#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Animation/Native/AnimationPlaybackFunctions.h"
#include "Game/HeroPawn/Equipment/HeroEquipmentState.h"

#include <cstdint>
#include <string>

namespace fable::game
{
    class Entity;
    class EntityService;
}

namespace fable::game::hero_pawn::equipment::transitions
{
    // Replays one reliable owner-observed draw/stow action on a remote Hero.
    // The body animation starts first; the exact final CTCCarrying slots are
    // applied at the same point at which the retail action mutates them.
    class RemoteHeroWeaponTransitionController final
    {
    public:
        bool Initialize(
            game::EntityService& entities,
            const core::Diagnostics& diagnostics) noexcept;
        void Bind(
            game::Entity& hero,
            void* nativeHero,
            std::uint64_t actorId) noexcept;
        bool Submit(
            const HeroEquipmentState& finalState,
            const std::string& sourceActionType,
            std::uint32_t animationId);
        void Process(std::uint64_t now) noexcept;
        [[nodiscard]] bool ConsumeAppliedState(
            HeroEquipmentState& state) noexcept;
        void Unbind() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] bool IsPending() const noexcept;

    private:
        static constexpr std::uint64_t CarryMutationDelayMilliseconds = 200;
        static constexpr std::uint64_t TransitionLifetimeMilliseconds = 1'000;
        static constexpr std::uint64_t MutationRetryMilliseconds = 75;
        static constexpr std::uint32_t MaximumMutationAttempts = 3;

        struct PendingTransition final
        {
            HeroEquipmentState finalState;
            std::string sourceActionType;
            std::uint64_t mutationAt = 0;
            std::uint64_t expiresAt = 0;
            std::uint32_t animationId = 0;
            std::uint32_t mutationAttempts = 0;
            bool active = false;
        };

        game::EntityService* entities_ = nullptr;
        game::Entity* hero_ = nullptr;
        void* nativeHero_ = nullptr;
        std::uint64_t actorId_ = 0;
        game::creature::animation::native::AnimationPlaybackFunctions
            animation_;
        PendingTransition pending_;
        HeroEquipmentState appliedState_ = {};
        bool appliedStateAvailable_ = false;
        core::Diagnostics diagnostics_ = {};
        bool initialized_ = false;
    };
}
