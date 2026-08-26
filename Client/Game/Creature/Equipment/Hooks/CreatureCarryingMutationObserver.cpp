#include "CreatureCarryingMutationObserver.h"

#include <climits>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr std::uintptr_t kAttachRva = 0x01956650;
    constexpr std::uintptr_t kAttachExceptionHandlerRva = 0x0252A968;
    constexpr std::uintptr_t kRemoveRva = 0x01955AA0;
    constexpr std::array<std::uint8_t, 12> kAttachBodySignature = {
        0x64, 0xA1, 0x00, 0x00, 0x00, 0x00,
        0x50, 0x83, 0xEC, 0x28, 0x53, 0x55};
    constexpr std::array<std::uint8_t, 5> kRemoveSignature = {
        0x53, 0x8B, 0x5C, 0x24, 0x08};

    bool ValidateAttach(
        const std::uint8_t* target,
        std::uintptr_t expectedExceptionHandler) noexcept
    {
        if (target == nullptr)
        {
            return false;
        }
        __try
        {
            std::uintptr_t exceptionHandler = 0;
            if (target[0] != 0x6A || target[1] != 0xFF ||
                target[2] != 0x68)
            {
                return false;
            }
            std::memcpy(
                &exceptionHandler,
                target + 3,
                sizeof(exceptionHandler));
            return exceptionHandler == expectedExceptionHandler &&
                std::memcmp(
                    target + 7,
                    kAttachBodySignature.data(),
                    kAttachBodySignature.size()) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace fable::game::creature::equipment::hooks
{
    CreatureCarryingMutationObserver*
        CreatureCarryingMutationObserver::active_ = nullptr;

    bool CreatureCarryingMutationObserver::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        diagnostics_ = diagnostics;
#if !defined(_M_IX86)
        return false;
#else
        if (gameModule == nullptr ||
            (active_ != nullptr && active_ != this))
        {
            return false;
        }
        auto* const module = reinterpret_cast<std::uint8_t*>(gameModule);
        auto* const attachTarget = module + kAttachRva;
        auto* const removeTarget = module + kRemoveRva;
        if (!ValidateAttach(
                attachTarget,
                reinterpret_cast<std::uintptr_t>(gameModule) +
                    kAttachExceptionHandlerRva) ||
            std::memcmp(
                removeTarget,
                kRemoveSignature.data(),
                kRemoveSignature.size()) != 0)
        {
            diagnostics_.Event(
                "CreatureCarryingMutationObserverRejected",
                "retail CTCCarrying mutation signatures did not match");
            return false;
        }

        active_ = this;
        if (!InstallDetour(
                removeTarget,
                kRemoveSignature.data(),
                kRemoveSignature.size(),
                kRemoveSignature.size(),
                reinterpret_cast<void*>(
                    &CreatureCarryingMutationObserver::ObserveRemove),
                removeDetour_))
        {
            active_ = nullptr;
            return false;
        }
        originalRemove_ = reinterpret_cast<Remove>(removeDetour_.patch.Original());
        if (!InstallDetour(
                attachTarget,
                attachTarget,
                7,
                7,
                reinterpret_cast<void*>(
                    &CreatureCarryingMutationObserver::ObserveAttach),
                attachDetour_))
        {
            const bool removeRestored = RestoreDetour(removeDetour_);
            if (!removeRestored)
            {
                diagnostics_.Log(
                    "Hook: creature-carrying attach install failed and the remove detour could not be rolled back; callback state retained.");
                return false;
            }
            if (removeDetour_.patch.ProtectionRestoreFailed())
            {
                diagnostics_.Log(
                    "Hook: creature-carrying remove rollback restored bytes, but code protection restoration failed.");
            }
            removeDetour_.target = nullptr;
            removeDetour_.displacedBytes = 0;
            originalRemove_ = nullptr;
            active_ = nullptr;
            return false;
        }
        originalAttach_ = reinterpret_cast<Attach>(attachDetour_.patch.Original());

        char detail[224] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "attach=%p attach_trampoline=%p remove=%p remove_trampoline=%p",
            attachDetour_.target,
            attachDetour_.patch.Original(),
            removeDetour_.target,
            removeDetour_.patch.Original());
        diagnostics_.Event("CreatureCarryingMutationObserverReady", detail);
        return true;
#endif
    }

    void CreatureCarryingMutationObserver::Shutdown() noexcept
    {
        const bool attachRemoved = RestoreDetour(attachDetour_);
        const bool removeRemoved = RestoreDetour(removeDetour_);
        if (!attachRemoved || !removeRemoved)
        {
            diagnostics_.Log(
                "Hook: creature-carrying shutdown skipped because a target changed.");
            return;
        }
        if (attachDetour_.patch.ProtectionRestoreFailed() ||
            removeDetour_.patch.ProtectionRestoreFailed())
        {
            diagnostics_.Log(
                "Hook: creature-carrying bytes restored, but code protection restoration failed.");
        }
        attachDetour_.target = nullptr;
        attachDetour_.displacedBytes = 0;
        removeDetour_.target = nullptr;
        removeDetour_.displacedBytes = 0;
        SetEventSink(nullptr, nullptr);
        if (active_ == this) active_ = nullptr;
        originalAttach_ = nullptr;
        originalRemove_ = nullptr;
        diagnostics_ = {};
    }

    void CreatureCarryingMutationObserver::SetEventSink(
        EventSink sink,
        void* context) noexcept
    {
        if (sink == nullptr)
        {
            eventSink_.store(nullptr, std::memory_order_release);
            eventSinkContext_.store(nullptr, std::memory_order_release);
            return;
        }
        eventSinkContext_.store(context, std::memory_order_release);
        eventSink_.store(sink, std::memory_order_release);
    }

    bool CreatureCarryingMutationObserver::IsInstalled() const noexcept
    {
        return active_ == this && originalAttach_ != nullptr &&
            originalRemove_ != nullptr && attachDetour_.patch.IsInstalled() &&
            removeDetour_.patch.IsInstalled();
    }

    bool CreatureCarryingMutationObserver::InstallDetour(
        std::uint8_t* target,
        const void* expected,
        std::size_t expectedSize,
        std::size_t displacedBytes,
        void* replacement,
        Detour& detour) noexcept
    {
        if (target == nullptr || replacement == nullptr ||
            expected == nullptr || expectedSize == 0 ||
            detour.patch.IsInstalled())
        {
            return false;
        }
        if (!detour.patch.Install(
                target, expected, expectedSize, replacement, displacedBytes))
        {
            return false;
        }
        detour.target = target;
        detour.displacedBytes = displacedBytes;
        return true;
    }

    bool CreatureCarryingMutationObserver::RestoreDetour(
        Detour& detour) noexcept
    {
        if (!detour.patch.IsInstalled())
        {
            return true;
        }
        if (!detour.patch.Shutdown())
        {
            return false;
        }
        return true;
    }

    void CreatureCarryingMutationObserver::Publish(
        const CreatureCarryingMutationEvent& event) noexcept
    {
        const EventSink sink = eventSink_.load(std::memory_order_acquire);
        if (sink != nullptr)
        {
            sink(
                eventSinkContext_.load(std::memory_order_acquire),
                event);
        }
    }

    void __fastcall CreatureCarryingMutationObserver::ObserveAttach(
        void* component,
        void*,
        void* thing,
        std::int32_t carrySlot,
        bool primary)
    {
        CreatureCarryingMutationObserver* const observer = active_;
        if (observer == nullptr || observer->originalAttach_ == nullptr)
        {
            return;
        }
        observer->originalAttach_(
            component, thing, carrySlot, primary);
        observer->Publish({
            CreatureCarryingMutationKind::Attached,
            component,
            thing,
            carrySlot});
    }

    std::uintptr_t __fastcall
        CreatureCarryingMutationObserver::ObserveRemove(
            void* component,
            void*,
            void* thing)
    {
        CreatureCarryingMutationObserver* const observer = active_;
        if (observer == nullptr || observer->originalRemove_ == nullptr)
        {
            return 0;
        }
        const std::uintptr_t result = observer->originalRemove_(
            component, thing);
        observer->Publish({
            CreatureCarryingMutationKind::Removed,
            component,
            thing,
            0});
        return result;
    }
}
