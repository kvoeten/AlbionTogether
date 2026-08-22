#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Actions/CreatureActionLifecycleEvent.h"
#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fable::game::hero_pawn::abilities::hooks
{
    // The retail pillar CTCs submit BuildUp during the same simulation frame
    // in which their short Cast action retires. Preserve that exact native
    // action when priority arbitration rejects it, then retry it immediately
    // after the creature action update. Fable continues to own BuildUp,
    // Release/ReleaseAndLoop, animation, targeting, and VFX.
    class PillarAbilityLifecycleHook final
    {
    public:
        ~PillarAbilityLifecycleHook();

        bool Install(
            game::creature::actions::CreatureActionLifecycleObserver& observer,
            const core::Diagnostics& diagnostics);
        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        enum class PillarKind : std::uint8_t
        {
            None,
            DivineWrath,
            UnholyPower,
        };

        struct PendingAction final
        {
            void* creature = nullptr;
            void* action = nullptr;
            std::uint64_t thingUid = 0;
            std::uint64_t capturedAt = 0;
            PillarKind kind = PillarKind::None;
            bool authoritativeReplay = false;
            bool inFlight = false;
        };

        static constexpr std::size_t PendingCapacity = 64;
        static constexpr std::size_t ActionNameCapacity = 128;
        static constexpr std::uint64_t PendingLifetimeMilliseconds = 5'000;
        static constexpr std::uint64_t SweepIntervalMilliseconds = 250;
        static constexpr unsigned int DiagnosticLimit = 128;

        static void OnActionEvent(
            void* context,
            const game::creature::actions::CreatureActionLifecycleEvent& event);
        static void OnPostUpdate(
            void* context,
            game::creature::actions::CreatureActionLifecycleObserver& observer,
            void* creature);

        void CaptureRejectedBuildUp(
            const game::creature::actions::CreatureActionLifecycleEvent& event)
            noexcept;
        void ReportPillarComponentState(
            const game::creature::actions::CreatureActionLifecycleEvent& event)
            noexcept;
        void RetryAfterActionUpdate(
            game::creature::actions::CreatureActionLifecycleObserver& observer,
            void* creature) noexcept;
        void SweepExpired(std::uint64_t now) noexcept;
        void ReleaseAllPending() noexcept;

        static PillarKind ResolveBuildUp(const char* actionType) noexcept;
        static PillarKind ResolvePillarAction(const char* actionType) noexcept;
        static bool MatchesCast(
            PillarKind kind,
            const char* actionType) noexcept;
        static const char* Name(PillarKind kind) noexcept;
        static void* ReadActiveAction(void* creature) noexcept;
        static bool IsFinished(void* action) noexcept;
        static void* CloneAction(void* action) noexcept;
        static void DestroyAction(void* action) noexcept;
        static void* FindPillarComponent(
            void* creature,
            PillarKind kind) noexcept;
        static bool DescribeNativeType(
            void* object,
            char* name,
            std::size_t capacity) noexcept;

        game::creature::actions::CreatureActionLifecycleObserver* observer_ =
            nullptr;
        core::Diagnostics diagnostics_ = {};
        mutable SRWLOCK pendingLock_ = SRWLOCK_INIT;
        std::array<PendingAction, PendingCapacity> pending_ = {};
        std::atomic_uint64_t lastSweepAt_{0};
        std::atomic_uint diagnosticCount_{0};
    };
}
