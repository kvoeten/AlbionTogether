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
                kRemoveSignature.size(),
                reinterpret_cast<void*>(
                    &CreatureCarryingMutationObserver::ObserveRemove),
                removeDetour_))
        {
            active_ = nullptr;
            return false;
        }
        originalRemove_ = reinterpret_cast<Remove>(removeDetour_.trampoline);
        if (!InstallDetour(
                attachTarget,
                7,
                reinterpret_cast<void*>(
                    &CreatureCarryingMutationObserver::ObserveAttach),
                attachDetour_))
        {
            RestoreDetour(removeDetour_);
            originalRemove_ = nullptr;
            active_ = nullptr;
            return false;
        }
        originalAttach_ = reinterpret_cast<Attach>(attachDetour_.trampoline);

        char detail[224] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "attach=%p attach_trampoline=%p remove=%p remove_trampoline=%p",
            attachDetour_.target,
            attachDetour_.trampoline,
            removeDetour_.target,
            removeDetour_.trampoline);
        diagnostics_.Event("CreatureCarryingMutationObserverReady", detail);
        return true;
#endif
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
            originalRemove_ != nullptr && attachDetour_.target != nullptr &&
            removeDetour_.target != nullptr;
    }

    bool CreatureCarryingMutationObserver::InstallDetour(
        std::uint8_t* target,
        std::size_t displacedBytes,
        void* replacement,
        Detour& detour) noexcept
    {
        if (target == nullptr || replacement == nullptr ||
            displacedBytes < 5 ||
            displacedBytes > detour.originalBytes.size())
        {
            return false;
        }
        auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
            nullptr,
            displacedBytes + 5,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE));
        if (trampoline == nullptr)
        {
            return false;
        }
        std::memcpy(detour.originalBytes.data(), target, displacedBytes);
        std::memcpy(trampoline, target, displacedBytes);
        trampoline[displacedBytes] = 0xE9;
        const std::intptr_t resume =
            reinterpret_cast<std::intptr_t>(target + displacedBytes) -
            (reinterpret_cast<std::intptr_t>(trampoline + displacedBytes) + 5);
        const std::intptr_t redirect =
            reinterpret_cast<std::intptr_t>(replacement) -
            (reinterpret_cast<std::intptr_t>(target) + 5);
        if (resume < INT32_MIN || resume > INT32_MAX ||
            redirect < INT32_MIN || redirect > INT32_MAX)
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        const auto resumeRelative = static_cast<std::int32_t>(resume);
        std::memcpy(
            trampoline + displacedBytes + 1,
            &resumeRelative,
            sizeof(resumeRelative));

        std::array<std::uint8_t, 7> patch = {};
        patch.fill(0x90);
        patch[0] = 0xE9;
        const auto redirectRelative = static_cast<std::int32_t>(redirect);
        std::memcpy(
            patch.data() + 1,
            &redirectRelative,
            sizeof(redirectRelative));
        DWORD previousProtection = 0;
        if (!VirtualProtect(
                target,
                displacedBytes,
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        std::memcpy(target, patch.data(), displacedBytes);
        FlushInstructionCache(GetCurrentProcess(), target, displacedBytes);
        FlushInstructionCache(
            GetCurrentProcess(), trampoline, displacedBytes + 5);
        DWORD discarded = 0;
        VirtualProtect(
            target, displacedBytes, previousProtection, &discarded);
        detour.target = target;
        detour.trampoline = trampoline;
        detour.displacedBytes = displacedBytes;
        return true;
    }

    void CreatureCarryingMutationObserver::RestoreDetour(
        Detour& detour) noexcept
    {
        if (detour.target != nullptr && detour.displacedBytes != 0)
        {
            DWORD previousProtection = 0;
            if (VirtualProtect(
                    detour.target,
                    detour.displacedBytes,
                    PAGE_EXECUTE_READWRITE,
                    &previousProtection))
            {
                std::memcpy(
                    detour.target,
                    detour.originalBytes.data(),
                    detour.displacedBytes);
                FlushInstructionCache(
                    GetCurrentProcess(),
                    detour.target,
                    detour.displacedBytes);
                DWORD discarded = 0;
                VirtualProtect(
                    detour.target,
                    detour.displacedBytes,
                    previousProtection,
                    &discarded);
            }
        }
        if (detour.trampoline != nullptr)
        {
            VirtualFree(detour.trampoline, 0, MEM_RELEASE);
        }
        detour = {};
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
