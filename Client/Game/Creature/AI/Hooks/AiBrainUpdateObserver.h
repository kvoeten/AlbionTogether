#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"
#include "Game/Creature/AI/Native/AiBrainFunctions.h"

#include <Windows.h>

#include <array>
#include <atomic>

namespace fable::game::creature::ai
{
    class AiBrainUpdateObserver final
    {
    public:
        using ExecutionSink = bool(*)(void* context, void* ownerThing);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;
        void SetExecutionSink(ExecutionSink sink, void* context) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] unsigned int ObservedBrainCount() const noexcept;

    private:
        static constexpr std::size_t TrackedBrainLimit = 32;

        static void __fastcall Observe(void* brain, void* unused);
        bool TrackFirstUpdate(void* brain, unsigned int& ordinal) noexcept;
        static void* ResolveOwnerThing(void* brain) noexcept;
        bool ShouldExecute(void* ownerThing) const noexcept;
        void Report(
            void* brain,
            void* ownerThing,
            unsigned int ordinal,
            bool executed) const;

        static AiBrainUpdateObserver* active_;

        core::Diagnostics diagnostics_ = {};
        native::AiBrainFunctions::UpdatePointer original_ = nullptr;
        core::hooking::CodePatch vtablePatch_;
        std::array<std::atomic<void*>, TrackedBrainLimit> trackedBrains_ = {};
        std::atomic_uint observedBrainCount_{0};
        std::atomic<ExecutionSink> executionSink_{nullptr};
        std::atomic<void*> executionSinkContext_{nullptr};
    };
}
