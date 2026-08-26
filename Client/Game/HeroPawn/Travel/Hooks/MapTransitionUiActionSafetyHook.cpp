#include "MapTransitionUiActionSafetyHook.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr std::uintptr_t UiManagerVtableRva = 0x02B21BD4;
    constexpr std::size_t UiManagerActionVtableIndex = 13;
    constexpr std::uintptr_t UiManagerActionRva = 0x01B7BB67;
    constexpr std::size_t UiActionRegistryStateOffset = 0x48;
    constexpr std::uintptr_t LowestReadableAddress = 0x10000;

    constexpr std::array<std::uint8_t, 8> UiManagerActionPrefix = {
        0x55, 0x83, 0xEC, 0x70, 0x8D, 0x6C, 0x24, 0xFC
    };

    bool IsRangeInsideImage(
        HMODULE gameModule,
        std::uintptr_t rva,
        std::size_t size) noexcept
    {
        if (gameModule == nullptr || size == 0)
        {
            return false;
        }
        const auto* const base = reinterpret_cast<const std::uint8_t*>(
            gameModule);
        const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(
            base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
        {
            return false;
        }
        const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
        {
            return false;
        }
        const std::size_t imageSize = nt->OptionalHeader.SizeOfImage;
        return rva < imageSize && size <= imageSize - rva;
    }

    bool ReadActionState(
        void* manager,
        void* action,
        std::uintptr_t& registryState,
        unsigned int& actionType) noexcept
    {
        registryState = 0;
        actionType = 0;
        if (manager == nullptr || action == nullptr)
        {
            return false;
        }

#if defined(_MSC_VER)
        __try
        {
            registryState = *reinterpret_cast<const std::uintptr_t*>(
                static_cast<const std::uint8_t*>(manager) +
                UiActionRegistryStateOffset);
            void* const nativeAction = *reinterpret_cast<void* const*>(action);
            if (nativeAction != nullptr)
            {
                actionType = *static_cast<const unsigned int*>(nativeAction);
            }
            return registryState >= LowestReadableAddress;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            registryState = 0;
            actionType = 0;
            return false;
        }
#else
        return false;
#endif
    }
}

namespace fable::game::hero_pawn::travel::hooks
{
    MapTransitionUiActionSafetyHook* MapTransitionUiActionSafetyHook::active_ =
        nullptr;

    bool MapTransitionUiActionSafetyHook::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        if (active_ != nullptr && active_ != this)
        {
            return false;
        }

#if !defined(_M_IX86)
        diagnostics.Log(
            "Hook: map-transition UI action safety is only supported by the x86 client.");
        return false;
#else
        if (!IsRangeInsideImage(
                gameModule,
                UiManagerActionRva,
                UiManagerActionPrefix.size()) ||
            !IsRangeInsideImage(
                gameModule,
                UiManagerVtableRva +
                    UiManagerActionVtableIndex * sizeof(void*),
                sizeof(void*)))
        {
            return false;
        }

        auto* const base = reinterpret_cast<std::uint8_t*>(gameModule);
        auto* const expected = base + UiManagerActionRva;
        if (std::memcmp(
                expected,
                UiManagerActionPrefix.data(),
                UiManagerActionPrefix.size()) != 0)
        {
            diagnostics.Log(
                "Hook: map-transition UI action target failed current-build ABI validation.");
            return false;
        }

        void** const slot = reinterpret_cast<void**>(
            base + UiManagerVtableRva) + UiManagerActionVtableIndex;
        if (*slot != expected)
        {
            diagnostics.Log(
                "Hook: NUISystem CManager action vtable failed current-build ABI validation.");
            return false;
        }

        DWORD previousProtection = 0;
        if (!VirtualProtect(
                slot, sizeof(*slot), PAGE_READWRITE, &previousProtection))
        {
            return false;
        }

        diagnostics_ = diagnostics;
        vtableSlot_ = slot;
        original_ = reinterpret_cast<ActionFunction>(*slot);
        active_ = this;
        *slot = reinterpret_cast<void*>(
            &MapTransitionUiActionSafetyHook::Intercept);
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
        DWORD discardedProtection = 0;
        VirtualProtect(
            slot,
            sizeof(*slot),
            previousProtection,
            &discardedProtection);

        diagnostics_.Log(
            "Hook: map-transition UI action registry safety installed.");
        diagnostics_.Event(
            "MapTransitionUiActionSafetyReady",
            "queued UI actions are held while the destination registry is incomplete");
        return true;
#endif
    }

    void MapTransitionUiActionSafetyHook::Shutdown() noexcept
    {
        if (active_ == this && vtableSlot_ != nullptr && original_ != nullptr)
        {
            DWORD previousProtection = 0;
            if (VirtualProtect(
                    vtableSlot_,
                    sizeof(*vtableSlot_),
                    PAGE_READWRITE,
                    &previousProtection))
            {
                if (*vtableSlot_ == reinterpret_cast<void*>(&Intercept))
                {
                    *vtableSlot_ = reinterpret_cast<void*>(original_);
                    FlushInstructionCache(
                        GetCurrentProcess(),
                        vtableSlot_,
                        sizeof(*vtableSlot_));
                }
                DWORD discardedProtection = 0;
                VirtualProtect(
                    vtableSlot_,
                    sizeof(*vtableSlot_),
                    previousProtection,
                    &discardedProtection);
            }
            active_ = nullptr;
        }
        suppressedEvents_.store(0, std::memory_order_release);
        vtableSlot_ = nullptr;
        original_ = nullptr;
        diagnostics_ = {};
    }

    bool MapTransitionUiActionSafetyHook::IsInstalled() const noexcept
    {
        return active_ == this && vtableSlot_ != nullptr &&
            original_ != nullptr;
    }

    void __fastcall MapTransitionUiActionSafetyHook::Intercept(
        void* manager,
        void*,
        void* action)
    {
        MapTransitionUiActionSafetyHook* const hook = active_;
        if (hook == nullptr || hook->original_ == nullptr)
        {
            return;
        }

        std::uintptr_t registryState = 0;
        unsigned int actionType = 0;
        if (!ReadActionState(
                manager, action, registryState, actionType))
        {
            hook->LogSuppressed(registryState, actionType);
            return;
        }
        hook->original_(manager, action);
    }

    void MapTransitionUiActionSafetyHook::LogSuppressed(
        std::uintptr_t registryState,
        unsigned int actionType) noexcept
    {
        const unsigned int ordinal = suppressedEvents_.fetch_add(
            1, std::memory_order_acq_rel);
        if (ordinal >= 8)
        {
            return;
        }
        char detail[192] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "action_type=0x%X registry_state=%p transition_frame_skipped=true",
            actionType,
            reinterpret_cast<void*>(registryState));
        diagnostics_.Event("MapTransitionUiActionSuppressed", detail);
    }
}
