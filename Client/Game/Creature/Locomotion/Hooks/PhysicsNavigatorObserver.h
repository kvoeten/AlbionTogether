#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Locomotion/Native/PhysicsNavigatorFunctions.h"

#include <Windows.h>

#include <array>
#include <atomic>

namespace fable::game::creature::locomotion
{
    class PhysicsNavigatorObserver final
    {
    public:
        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] unsigned int RequestCount() const noexcept;

    private:
        static constexpr std::size_t TrackedNavigatorLimit = 32;
        static constexpr std::size_t AnimationSnapshotByteCount = 0xC0;
        static constexpr std::size_t AnimationSnapshotDwordCount =
            AnimationSnapshotByteCount / sizeof(std::uint32_t);

        struct Snapshot
        {
            bool valid = false;
            void* ownerCandidate = nullptr;
            void* animationComplex = nullptr;
            void* animationState = nullptr;
            bool animationSnapshotValid = false;
            std::uint32_t animationStateHash = 0;
            std::array<std::uint32_t, AnimationSnapshotDwordCount>
                animationStateDwords = {};
            bool ownerMotionValid = false;
            float ownerMotionX = 0.0f;
            float ownerMotionY = 0.0f;
            Vector3 worldPosition = {};
            Vector3 vectorAt28 = {};
            Vector3 desiredPosition = {};
            Vector3 vectorAt95 = {};
            std::uint8_t stateFlags = 0;
            std::uint8_t collisionFlags = 0;
        };

        static void __fastcall ObserveRequest(
            void* navigator,
            void* unused,
            const Vector3* desiredPosition);
        static void __fastcall ObserveUpdate(void* navigator, void* unused);

        static Snapshot Capture(void* navigator) noexcept;
        int Track(void* navigator) noexcept;
        int Find(void* navigator) const noexcept;
        void ReportRequest(
            void* navigator,
            const Vector3* requested,
            const Snapshot& after,
            void* caller,
            unsigned int ordinal) const;
        void ReportUpdate(
            void* navigator,
            const Snapshot& before,
            const Snapshot& after,
            unsigned int ordinal) const;
        void ReportAnimationTransition(
            void* navigator,
            const Snapshot& previousFrame,
            const Snapshot& currentFrame,
            unsigned int ordinal) const;

        static PhysicsNavigatorObserver* active_;

        core::Diagnostics diagnostics_ = {};
        HMODULE gameModule_ = nullptr;
        native::PhysicsNavigatorFunctions::RequestNextPositionPointer
            originalRequest_ = nullptr;
        native::PhysicsNavigatorFunctions::UpdateMovementPointer
            originalUpdate_ = nullptr;
        void** requestVtableSlot_ = nullptr;
        void** updateVtableSlot_ = nullptr;
        std::array<std::atomic<void*>, TrackedNavigatorLimit>
            trackedNavigators_ = {};
        std::array<std::atomic_uint, TrackedNavigatorLimit> requestCounts_ = {};
        std::array<std::atomic_uint, TrackedNavigatorLimit> updateCounts_ = {};
        std::array<std::atomic_uint, TrackedNavigatorLimit>
            animationTransitionCounts_ = {};
        std::array<Snapshot, TrackedNavigatorLimit> lastSnapshots_ = {};
        std::atomic_uint requestCount_{0};
    };
}
