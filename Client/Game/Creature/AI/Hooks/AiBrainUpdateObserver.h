#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"
#include "Game/Creature/AI/Native/AiBrainFunctions.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <mutex>

namespace fable::game::creature::ai
{
    class AiBrainUpdateObserver final
    {
    public:
        using ExecutionSink = bool(*)(void* context, void* ownerThing);
        using StateGroupExecutionSink = bool(*)(
            void* context,
            void* creature,
            int frameTime,
            void* nativeProposal);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;
        void SetExecutionSink(ExecutionSink sink, void* context) noexcept;
        void SetStateGroupExecutionSink(
            StateGroupExecutionSink sink,
            void* context) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] unsigned int ObservedBrainCount() const noexcept;

    private:
        static constexpr std::size_t TrackedBrainLimit = 32;

        static void __fastcall Observe(void* brain, void* unused);
        static bool __fastcall ObserveStateGroup(
            void* creature,
            void* unused,
            int frameTime,
            void* nativeProposal);
        bool TrackFirstUpdate(void* brain, unsigned int& ordinal) noexcept;
        static void* ResolveOwnerThing(void* brain) noexcept;
        bool ShouldExecute(void* ownerThing) const noexcept;
        bool ShouldExecuteStateGroup(
            void* creature,
            int frameTime,
            void* nativeProposal) const noexcept;
        void Report(
            void* brain,
            void* ownerThing,
            unsigned int ordinal,
            bool executed) const;

        static std::atomic<AiBrainUpdateObserver*> active_;
        static std::mutex stateGroupLeaseMutex_;
        static std::atomic<native::AiBrainFunctions::UpdatePointer> processBrainOriginal_;
        // The direct state-group hook is intentionally process-lifetime. Its
        // replacement becomes a native passthrough after observer shutdown,
        // so no in-flight call can execute a freed trampoline.
        static core::hooking::InlineHook* stateGroupProcessHook_;
        // Always points at InlineHook::Original(), never at the patched
        // dispatcher entry (which would recurse through ObserveStateGroup).
        static std::atomic<native::AiBrainFunctions::StateGroupDecisionPointer>
            stateGroupProcessOriginal_;

        core::Diagnostics diagnostics_ = {};
        native::AiBrainFunctions::UpdatePointer original_ = nullptr;
        core::hooking::CodePatch vtablePatch_;
        std::array<std::atomic<void*>, TrackedBrainLimit> trackedBrains_ = {};
        std::atomic_uint observedBrainCount_{0};
        std::atomic<ExecutionSink> executionSink_{nullptr};
        std::atomic<void*> executionSinkContext_{nullptr};
        std::atomic<StateGroupExecutionSink> stateGroupExecutionSink_{nullptr};
        std::atomic<void*> stateGroupExecutionSinkContext_{nullptr};
    };
}
