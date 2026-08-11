#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/AI/Native/AiBrainFunctions.h"

#include <Windows.h>

#include <array>
#include <atomic>

namespace fable::game::creature::ai
{
    class AiBrainUpdateObserver final
    {
    public:
        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] unsigned int ObservedBrainCount() const noexcept;

    private:
        static constexpr std::size_t TrackedBrainLimit = 32;

        static void __fastcall Observe(void* brain, void* unused);
        bool TrackFirstUpdate(void* brain, unsigned int& ordinal) noexcept;
        void Report(void* brain, unsigned int ordinal) const;

        static AiBrainUpdateObserver* active_;

        core::Diagnostics diagnostics_ = {};
        native::AiBrainFunctions::UpdatePointer original_ = nullptr;
        void** vtableSlot_ = nullptr;
        std::array<std::atomic<void*>, TrackedBrainLimit> trackedBrains_ = {};
        std::atomic_uint observedBrainCount_{0};
    };
}
