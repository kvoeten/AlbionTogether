#include "AiBrainUpdateObserver.h"

#include <cstdint>
#include <cstdio>

namespace fable::game::creature::ai
{
    AiBrainUpdateObserver* AiBrainUpdateObserver::active_ = nullptr;

    bool AiBrainUpdateObserver::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        diagnostics_ = diagnostics;

#if !defined(_M_IX86)
        diagnostics_.Log("Hook: AI brain observation is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            diagnostics_.Log("Hook: another AI brain update observer is already active.");
            return false;
        }

        void** slot = nullptr;
        native::AiBrainFunctions::UpdatePointer original = nullptr;
        if (!native::AiBrainFunctions::ResolveUpdateSlot(
                gameModule,
                &slot,
                original))
        {
            diagnostics_.Log("Hook: CAIBrain update definition failed validation.");
            return false;
        }

        DWORD previousProtection = 0;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previousProtection))
        {
            diagnostics_.Log("Hook: CAIBrain update vtable protection change failed.");
            return false;
        }

        original_ = original;
        vtableSlot_ = slot;
        active_ = this;
        *slot = reinterpret_cast<void*>(&AiBrainUpdateObserver::Observe);
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));

        DWORD discarded = 0;
        if (!VirtualProtect(slot, sizeof(*slot), previousProtection, &discarded))
        {
            diagnostics_.Log("Hook: AI brain observer installed, but vtable protection restoration failed.");
        }

        char detail[192] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "slot=%p original=%p tracked_limit=%zu",
            slot,
            reinterpret_cast<void*>(original),
            TrackedBrainLimit);
        diagnostics_.Log("Hook: read-only CAIBrain decision-boundary observer installed.");
        diagnostics_.Event("AiBrainUpdateObserverReady", detail);
        return true;
#endif
    }

    bool AiBrainUpdateObserver::IsInstalled() const noexcept
    {
        return active_ == this && original_ != nullptr && vtableSlot_ != nullptr;
    }

    unsigned int AiBrainUpdateObserver::ObservedBrainCount() const noexcept
    {
        return observedBrainCount_.load(std::memory_order_acquire);
    }

    bool AiBrainUpdateObserver::TrackFirstUpdate(
        void* brain,
        unsigned int& ordinal) noexcept
    {
        ordinal = 0;
        for (auto& tracked : trackedBrains_)
        {
            if (tracked.load(std::memory_order_acquire) == brain)
            {
                return false;
            }
        }
        for (auto& tracked : trackedBrains_)
        {
            void* expected = nullptr;
            if (tracked.compare_exchange_strong(
                    expected,
                    brain,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                ordinal = observedBrainCount_.fetch_add(
                    1,
                    std::memory_order_acq_rel) + 1;
                return true;
            }
            if (expected == brain)
            {
                return false;
            }
        }
        return false;
    }

    void AiBrainUpdateObserver::Report(void* brain, unsigned int ordinal) const
    {
        void* fiber = nullptr;
        void* fiberVtable = nullptr;
        void* fiberDispatch = nullptr;
        void* definition = nullptr;
        void* context0 = nullptr;
        void* context1 = nullptr;
        bool fiberPaused = false;
        bool readable = false;
        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(brain);
            fiber = *reinterpret_cast<void* const*>(
                bytes + native::AiBrainFunctions::FiberOffset);
            definition = *reinterpret_cast<void* const*>(
                bytes + native::AiBrainFunctions::DefinitionOffset);
            context0 = *reinterpret_cast<void* const*>(
                bytes + native::AiBrainFunctions::ContextBeginOffset);
            context1 = *reinterpret_cast<void* const*>(
                bytes + native::AiBrainFunctions::ContextBeginOffset + sizeof(void*));
            if (fiber != nullptr)
            {
                const auto* const fiberBytes = static_cast<const std::uint8_t*>(fiber);
                fiberPaused = fiberBytes[native::AiBrainFunctions::FiberPausedOffset] != 0;
                fiberVtable = *reinterpret_cast<void* const*>(fiberBytes);
                if (fiberVtable != nullptr)
                {
                    fiberDispatch = static_cast<void**>(fiberVtable)[
                        native::AiBrainFunctions::FiberDispatchSlot];
                }
            }
            readable = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }

        char detail[384] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "brain=%p ordinal=%u fiber=%p fiber_vtable=%p dispatch=%p paused=%s definition=%p context0=%p owner_candidate=%p readable=%s thread=%lu",
            brain,
            ordinal,
            fiber,
            fiberVtable,
            fiberDispatch,
            fiberPaused ? "true" : "false",
            definition,
            context0,
            context1,
            readable ? "true" : "false",
            static_cast<unsigned long>(GetCurrentThreadId()));
        diagnostics_.Event("AiBrainFirstUpdateObserved", detail);
    }

    void __fastcall AiBrainUpdateObserver::Observe(void* brain, void*)
    {
        AiBrainUpdateObserver* const observer = active_;
        if (observer == nullptr || observer->original_ == nullptr)
        {
            return;
        }

        unsigned int ordinal = 0;
        const bool firstUpdate = brain != nullptr &&
            observer->TrackFirstUpdate(brain, ordinal);
        if (firstUpdate)
        {
            observer->Report(brain, ordinal);
        }
        observer->original_(brain);
    }
}
