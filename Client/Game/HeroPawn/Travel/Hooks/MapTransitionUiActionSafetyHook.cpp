#include "MapTransitionUiActionSafetyHook.h"

#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Native/Addresses.h"

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
    constexpr std::size_t UiManagerSetHeroTargetVtableIndex = 25;
    constexpr std::uintptr_t UiManagerSetHeroTargetRva = 0x01B7B89C;
    constexpr std::size_t UiManagerHeroTargetOffset = 0x6C;
    // The retail dispatcher reads this process-global NUI registry at
    // UiManagerActionRva + 0x26. It is not a field on the manager object.
    constexpr std::uintptr_t UiActionRegistryStateRva = 0x0322AE48;
    constexpr std::uintptr_t LowestReadableAddress = 0x10000;

    constexpr std::array<std::uint8_t, 8> UiManagerActionPrefix = {
        0x55, 0x83, 0xEC, 0x70, 0x8D, 0x6C, 0x24, 0xFC
    };
    constexpr std::array<std::uint8_t, 10> UiManagerSetHeroTargetBytes = {
        0x8B, 0x44, 0x24, 0x04, 0x89, 0x41, 0x6C, 0xC2, 0x04, 0x00
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
        void* const* registryStateSlot,
        void* action,
        std::uintptr_t& registryState,
        unsigned int& actionType) noexcept
    {
        registryState = 0;
        actionType = 0;
        if (registryStateSlot == nullptr || action == nullptr)
        {
            return false;
        }

#if defined(_MSC_VER)
        __try
        {
            registryState = reinterpret_cast<std::uintptr_t>(
                *registryStateSlot);
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
        EntityService& entities,
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
                sizeof(void*)) ||
            !IsRangeInsideImage(
                gameModule,
                UiManagerVtableRva +
                    UiManagerSetHeroTargetVtableIndex * sizeof(void*),
                sizeof(void*)) ||
            !IsRangeInsideImage(
                gameModule,
                UiManagerSetHeroTargetRva,
                UiManagerSetHeroTargetBytes.size()) ||
            !IsRangeInsideImage(
                gameModule,
                UiActionRegistryStateRva,
                sizeof(void*)))
        {
            return false;
        }

        auto* const base = reinterpret_cast<std::uint8_t*>(gameModule);
        auto* const expectedTarget = base + UiManagerActionRva;
        auto* const expectedSetHeroTarget =
            base + UiManagerSetHeroTargetRva;
        if (std::memcmp(
                expectedTarget,
                UiManagerActionPrefix.data(),
                UiManagerActionPrefix.size()) != 0 ||
            std::memcmp(
                expectedSetHeroTarget,
                UiManagerSetHeroTargetBytes.data(),
                UiManagerSetHeroTargetBytes.size()) != 0)
        {
            diagnostics.Log(
                "Hook: map-transition UI action target failed current-build ABI validation.");
            return false;
        }

        void** const managerVtable = reinterpret_cast<void**>(
            base + UiManagerVtableRva);
        void** const slot = managerVtable + UiManagerActionVtableIndex;
        if (*slot != expectedTarget ||
            managerVtable[UiManagerSetHeroTargetVtableIndex] !=
                expectedSetHeroTarget)
        {
            diagnostics.Log(
                "Hook: NUISystem CManager action vtable failed current-build ABI validation.");
            return false;
        }

        diagnostics_ = diagnostics;
        gameModule_ = gameModule;
        entities_ = &entities;
        managerVtable_ = managerVtable;
        registryStateSlot_ = reinterpret_cast<void**>(
            base + UiActionRegistryStateRva);
        setHeroTarget_ = reinterpret_cast<SetHeroTargetFunction>(
            expectedSetHeroTarget);
        original_ = reinterpret_cast<ActionFunction>(*slot);
        void* const expectedSlot = *slot;
        void* const replacement = reinterpret_cast<void*>(
            &MapTransitionUiActionSafetyHook::Intercept);
        if (!actionPatch_.Install(
                slot,
                &expectedSlot,
                sizeof(expectedSlot),
                &replacement,
                sizeof(replacement)))
        {
            setHeroTarget_ = nullptr;
            registryStateSlot_ = nullptr;
            managerVtable_ = nullptr;
            entities_ = nullptr;
            gameModule_ = nullptr;
            original_ = nullptr;
            return false;
        }
        active_ = this;

        diagnostics_.Log(
            "Hook: map-transition UI action registry safety installed.");
        diagnostics_.Event(
            "MapTransitionUiActionSafetyReady",
            "queued UI actions are held while the native registry or local Hero is incomplete");
        return true;
#endif
    }

    void MapTransitionUiActionSafetyHook::Shutdown() noexcept
    {
        const bool restored = actionPatch_.Shutdown();
        if (!restored)
        {
            diagnostics_.Event(
                "MapTransitionUiActionHookUninstallSkipped",
                "target-changed-by-another-hook");
            return;
        }
        if (actionPatch_.ProtectionRestoreFailed())
        {
            diagnostics_.Event(
                "MapTransitionUiActionHookProtectionRestoreWarning",
                "original-bytes-restored");
        }
        if (active_ == this)
        {
            active_ = nullptr;
        }
        suppressedEvents_.store(0, std::memory_order_release);
        repairedHeroTargetEvents_.store(0, std::memory_order_release);
        original_ = nullptr;
        setHeroTarget_ = nullptr;
        registryStateSlot_ = nullptr;
        managerVtable_ = nullptr;
        entities_ = nullptr;
        gameModule_ = nullptr;
        diagnostics_ = {};
    }

    bool MapTransitionUiActionSafetyHook::IsInstalled() const noexcept
    {
        return active_ == this && actionPatch_.IsInstalled() &&
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
        const bool registryReady = ReadActionState(
            hook->registryStateSlot_, action, registryState, actionType);
        if (!registryReady)
        {
            hook->LogSuppressed(
                registryState, actionType, "registry-incomplete");
            return;
        }

        void* const localHero = hook->ResolveLocalHero();
        if (localHero == nullptr)
        {
            hook->LogSuppressed(
                registryState, actionType, "local-hero-unavailable");
            return;
        }

        void* previousTarget = nullptr;
        if (!hook->RepairHeroTarget(manager, localHero, previousTarget))
        {
            hook->LogSuppressed(
                registryState, actionType, "hero-target-repair-failed");
            return;
        }
        if (previousTarget != localHero)
        {
            hook->LogHeroTargetRepaired(previousTarget, localHero);
        }
        hook->original_(manager, action);
    }

    void* MapTransitionUiActionSafetyHook::ResolveLocalHero() const noexcept
    {
        if (gameModule_ == nullptr || entities_ == nullptr)
        {
            return nullptr;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        Entity* const hero = entities_->GetHero();
        if (hero == nullptr)
        {
            return nullptr;
        }
        void* localHero = entities_->ResolveNative(hero->NativeHandle());
        hero->Release();

#if defined(_MSC_VER)
        __try
        {
            if (localHero == nullptr ||
                *reinterpret_cast<void**>(localHero) !=
                    reinterpret_cast<void*>(
                        base + native::rva::ThingPlayerCreatureVtable))
            {
                localHero = nullptr;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            localHero = nullptr;
        }
#else
        localHero = nullptr;
#endif
        return localHero;
    }

    bool MapTransitionUiActionSafetyHook::RepairHeroTarget(
        void* manager,
        void* localHero,
        void*& previousTarget) const noexcept
    {
        previousTarget = nullptr;
        if (manager == nullptr || localHero == nullptr ||
            managerVtable_ == nullptr || setHeroTarget_ == nullptr)
        {
            return false;
        }

#if defined(_MSC_VER)
        __try
        {
            if (*reinterpret_cast<void***>(manager) != managerVtable_ ||
                managerVtable_[UiManagerSetHeroTargetVtableIndex] !=
                    reinterpret_cast<void*>(setHeroTarget_))
            {
                return false;
            }
            previousTarget = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(manager) +
                UiManagerHeroTargetOffset);
            if (previousTarget != localHero)
            {
                setHeroTarget_(manager, localHero);
                if (*reinterpret_cast<void**>(
                        static_cast<std::uint8_t*>(manager) +
                        UiManagerHeroTargetOffset) != localHero)
                {
                    return false;
                }
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            previousTarget = nullptr;
            return false;
        }
#else
        return false;
#endif
    }

    void MapTransitionUiActionSafetyHook::LogSuppressed(
        std::uintptr_t registryState,
        unsigned int actionType,
        const char* reason) noexcept
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
            "action_type=0x%X registry_state=%p reason=%s transition_frame_skipped=true",
            actionType,
            reinterpret_cast<void*>(registryState),
            reason != nullptr ? reason : "unknown");
        diagnostics_.Event("MapTransitionUiActionSuppressed", detail);
    }

    void MapTransitionUiActionSafetyHook::LogHeroTargetRepaired(
        void* previousTarget,
        void* localHero) noexcept
    {
        const unsigned int ordinal = repairedHeroTargetEvents_.fetch_add(
            1, std::memory_order_acq_rel);
        if (ordinal >= 8)
        {
            return;
        }
        char detail[160] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "previous_target=%p local_hero=%p",
            previousTarget,
            localHero);
        diagnostics_.Event(
            "MapTransitionUiHeroTargetRepaired",
            detail);
    }

}
