#include "HeroEquipmentMutationObserver.h"

#include <climits>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr std::uintptr_t kReconcilePresentationRva = 0x01A492BD;
    constexpr std::array<std::uint8_t, 5> kSignature = {
        0x68, 0x00, 0x01, 0x00, 0x00};
}

namespace fable::game::hero_pawn::equipment::hooks
{
    HeroEquipmentMutationObserver* HeroEquipmentMutationObserver::active_ =
        nullptr;

    bool HeroEquipmentMutationObserver::Install(
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
        auto* const target = reinterpret_cast<std::uint8_t*>(gameModule) +
            kReconcilePresentationRva;
        if (std::memcmp(target, kSignature.data(), kSignature.size()) != 0)
        {
            diagnostics_.Event(
                "HeroEquipmentMutationObserverRejected",
                "retail weapon presentation signature did not match");
            return false;
        }
        active_ = this;
        if (!InstallDetour(
                target,
                reinterpret_cast<void*>(
                    &HeroEquipmentMutationObserver::Observe)))
        {
            active_ = nullptr;
            return false;
        }
        original_ = reinterpret_cast<ReconcilePresentation>(trampoline_);
        char detail[160] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "target=%p trampoline=%p function_rva=0x%08X",
            target_,
            trampoline_,
            static_cast<unsigned int>(kReconcilePresentationRva));
        diagnostics_.Event("HeroEquipmentMutationObserverReady", detail);
        return true;
#endif
    }

    void HeroEquipmentMutationObserver::Shutdown() noexcept
    {
        SetEventSink(nullptr, nullptr);
        if (active_ == this)
        {
            active_ = nullptr;
        }
        if (target_ != nullptr)
        {
            DWORD previousProtection = 0;
            if (VirtualProtect(
                    target_,
                    originalBytes_.size(),
                    PAGE_EXECUTE_READWRITE,
                    &previousProtection))
            {
                std::memcpy(
                    target_,
                    originalBytes_.data(),
                    originalBytes_.size());
                FlushInstructionCache(
                    GetCurrentProcess(),
                    target_,
                    originalBytes_.size());
                DWORD discarded = 0;
                VirtualProtect(
                    target_,
                    originalBytes_.size(),
                    previousProtection,
                    &discarded);
            }
        }
        if (trampoline_ != nullptr)
        {
            VirtualFree(trampoline_, 0, MEM_RELEASE);
        }
        original_ = nullptr;
        target_ = nullptr;
        trampoline_ = nullptr;
        originalBytes_.fill(0);
        diagnostics_ = {};
    }

    void HeroEquipmentMutationObserver::SetEventSink(
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

    bool HeroEquipmentMutationObserver::IsInstalled() const noexcept
    {
        return active_ == this && original_ != nullptr &&
            target_ != nullptr && trampoline_ != nullptr;
    }

    bool HeroEquipmentMutationObserver::InstallDetour(
        std::uint8_t* target,
        void* replacement) noexcept
    {
        auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
            nullptr,
            DisplacedBytes + 5,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE));
        if (trampoline == nullptr)
        {
            return false;
        }
        std::memcpy(originalBytes_.data(), target, DisplacedBytes);
        std::memcpy(trampoline, target, DisplacedBytes);
        trampoline[DisplacedBytes] = 0xE9;
        const std::intptr_t resume =
            reinterpret_cast<std::intptr_t>(target + DisplacedBytes) -
            (reinterpret_cast<std::intptr_t>(trampoline + DisplacedBytes) + 5);
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
            trampoline + DisplacedBytes + 1,
            &resumeRelative,
            sizeof(resumeRelative));
        std::array<std::uint8_t, DisplacedBytes> patch = {};
        patch[0] = 0xE9;
        const auto redirectRelative = static_cast<std::int32_t>(redirect);
        std::memcpy(
            patch.data() + 1,
            &redirectRelative,
            sizeof(redirectRelative));
        DWORD previousProtection = 0;
        if (!VirtualProtect(
                target,
                DisplacedBytes,
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        std::memcpy(target, patch.data(), patch.size());
        FlushInstructionCache(GetCurrentProcess(), target, patch.size());
        FlushInstructionCache(
            GetCurrentProcess(), trampoline, DisplacedBytes + 5);
        DWORD discarded = 0;
        VirtualProtect(
            target, DisplacedBytes, previousProtection, &discarded);
        target_ = target;
        trampoline_ = trampoline;
        return true;
    }

    void __fastcall HeroEquipmentMutationObserver::Observe(
        void* component,
        void*)
    {
        HeroEquipmentMutationObserver* const observer = active_;
        if (observer == nullptr || observer->original_ == nullptr)
        {
            return;
        }
        observer->original_(component);
        const EventSink sink = observer->eventSink_.load(
            std::memory_order_acquire);
        if (sink != nullptr)
        {
            sink(
                observer->eventSinkContext_.load(
                    std::memory_order_acquire),
                component);
        }
    }
}
