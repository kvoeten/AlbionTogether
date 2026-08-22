#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Creature/Locomotion/Native/CreatureModeManagerFunctions.h"
#include "Game/Math/Vector3.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace fable::game::creature::locomotion
{
    class CreatureModeManagerObserver final
    {
    public:
        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        static bool WatchOwner(void* nativeThing) noexcept;
        static bool BindAnimationMotionSource(
            void* sourcePlayerCreature,
            void* targetCreature) noexcept;
        static void ClearAnimationMotionSource() noexcept;
        static bool SetReplicatedAnimationMotion(
            void* targetCreature,
            const Vector3& linearVelocity,
            float angularVelocity) noexcept;
        static void ClearReplicatedAnimationMotion(
            void* targetCreature) noexcept;
        static void ClearReplicatedAnimationMotions() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] static unsigned int MirroredAnimationMotionCount() noexcept;

    private:
        static constexpr std::size_t ModeSnapshotDwordCount = 16;
        static constexpr std::size_t WatchedOwnerLimit = 3;

        struct Snapshot
        {
            bool valid = false;
            void* owner = nullptr;
            void* sentinel = nullptr;
            std::uint32_t count = 0;
            void* activeNode = nullptr;
            void* activeMode = nullptr;
            void* activeModeVtable = nullptr;
            std::uint32_t activeModeHash = 0;
            std::array<std::uint32_t, ModeSnapshotDwordCount> modeDwords = {};
        };

        struct ReplicatedAnimationMotion final
        {
            SRWLOCK valueLock = SRWLOCK_INIT;
            Vector3 linearVelocity = {};
            float angularVelocity = 0.0f;
            std::atomic_uint64_t updatedAt{0};
            std::atomic_uint64_t evaluatedAt{0};
        };

        [[nodiscard]] std::shared_ptr<ReplicatedAnimationMotion>
            FindReplicatedAnimationMotion(void* owner) const noexcept;
        [[nodiscard]] bool ReadReplicatedAnimationMotion(
            void* owner,
            std::uint64_t now,
            Vector3& linearVelocity,
            float& angularVelocity,
            float& evaluationSeconds) const noexcept;

        static bool __fastcall ObserveAddSource(
            void* manager,
            void* unused,
            int source);
        static void __fastcall ObserveRemoveSource(
            void* manager,
            void* unused,
            int source);
        static void __fastcall ObserveLocomotionEvaluation(
            void* mode,
            void* unused,
            void* evaluationContext);

        static Snapshot Capture(void* manager) noexcept;
        void Report(
            const char* state,
            void* manager,
            int source,
            bool result,
            const Snapshot& before,
            const Snapshot& after,
            unsigned int ordinal) const;
        int FindWatchedOwner(void* owner) const noexcept;
        void ReportLocomotionEvaluation(
            void* mode,
            void* owner,
            void* evaluationContext,
            float motionX,
            float motionY,
            float angularVelocity,
            float evaluationSeconds,
            unsigned int ordinal) const;

        static CreatureModeManagerObserver* active_;

        core::Diagnostics diagnostics_ = {};
        HMODULE gameModule_ = nullptr;
        native::CreatureModeManagerFunctions::AddSourcePointer
            originalAddSource_ = nullptr;
        native::CreatureModeManagerFunctions::RemoveSourcePointer
            originalRemoveSource_ = nullptr;
        native::CreatureModeManagerFunctions::EvaluateLocomotionPointer
            originalEvaluateLocomotion_ = nullptr;
        void* addSourceTrampoline_ = nullptr;
        void* removeSourceTrampoline_ = nullptr;
        void* evaluateLocomotionTrampoline_ = nullptr;
        std::atomic_uint addCount_{0};
        std::atomic_uint removeCount_{0};
        std::array<std::atomic<void*>, WatchedOwnerLimit> watchedOwners_ = {};
        std::array<std::atomic_uint, WatchedOwnerLimit>
            locomotionEvaluationCounts_ = {};
        std::array<std::atomic_bool, WatchedOwnerLimit> lastMoving_ = {};
        std::atomic<void*> animationMotionSource_{nullptr};
        std::atomic<void*> animationMotionTarget_{nullptr};
        std::atomic_uint mirroredAnimationMotionCount_{0};
        mutable SRWLOCK replicatedAnimationMotionLock_ = SRWLOCK_INIT;
        std::unordered_map<
            void*,
            std::shared_ptr<ReplicatedAnimationMotion>>
                replicatedAnimationMotions_;
    };
}
