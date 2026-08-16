#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Look/Hooks/CreatureFacingInputRouterHook.h"
#include "Game/Math/Vector3.h"
#include "Multiplayer/Movement/ReplicatedMovementSample.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace fable::game
{
    class Entity;
}

namespace fable::game::creature::locomotion
{
    class CreatureLocomotionService;
}

namespace fable::multiplayer::movement
{
    // One map-scoped native actor movement consumer. It intentionally knows
    // nothing about players, NPC brains, transport, or authority policy. A
    // future authoritative movement channel can feed the same interface for
    // any replicated creature.
    class ReplicatedActorMovement final
    {
    public:
        using NativeInput = game::creature::look::
            CreatureFacingInputRouterHook::ReplicatedMovementInput;

        void Initialize(
            game::creature::locomotion::CreatureLocomotionService& locomotion,
            const core::Diagnostics& diagnostics);
        void Bind(
            game::Entity& actor,
            void* nativeActor,
            const ReplicatedMovementSample& sample,
            const std::string& localMap);
        void BindNative(
            void* nativeActor,
            const ReplicatedMovementSample& sample,
            const std::string& localMap);
        void Update(
            const ReplicatedMovementSample& sample,
            const std::string& localMap);
        void Detach() noexcept;

        bool Provide(void* nativeActor, NativeInput& input);

    private:
        static constexpr std::size_t MaximumSamples = 8;
        static constexpr std::uint64_t InterpolationDelayMilliseconds = 75;
        static constexpr std::uint64_t MaximumExtrapolationMilliseconds = 125;

        [[nodiscard]] bool Evaluate(
            std::uint64_t now,
            ReplicatedMovementSample& output) const noexcept;
        void DerivePresentationMotion(
            std::uint64_t now,
            ReplicatedMovementSample& sample) noexcept;
        [[nodiscard]] static bool IsNewerSequence(
            std::uint32_t candidate,
            std::uint32_t current) noexcept;
        void ObserveLocomotion(
            game::Entity& actor,
            std::uint64_t now);

        game::creature::locomotion::CreatureLocomotionService* locomotion_ =
            nullptr;
        core::Diagnostics diagnostics_ = {};
        std::mutex stateMutex_;
        std::array<ReplicatedMovementSample, MaximumSamples> samples_ = {};
        std::size_t sampleCount_ = 0;
        std::string localMap_;
        game::Entity* actor_ = nullptr;
        void* nativeActor_ = nullptr;
        game::Vector3 lastPresentationPosition_ = {};
        game::Vector3 presentationVelocity_ = {};
        float lastPresentationFacing_ = 0.0f;
        float presentationAngularVelocity_ = 0.0f;
        std::uint64_t lastPresentationAt_ = 0;
        bool presentationMotionReady_ = false;
        game::Vector3 locomotionStartPosition_ = {};
        std::uint32_t locomotionStartAnimationHash_ = 0;
        std::uint64_t lastObservationAt_ = 0;
        bool movementCommanded_ = false;
        bool walkingReported_ = false;
    };
}
