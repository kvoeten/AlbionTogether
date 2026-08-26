#include "HeroEquipmentMutationObserver.h"

#include <array>
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
        original_ = reinterpret_cast<ReconcilePresentation>(patch_.Original());
        char detail[160] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "target=%p trampoline=%p function_rva=0x%08X",
            target,
            patch_.Original(),
            static_cast<unsigned int>(kReconcilePresentationRva));
        diagnostics_.Event("HeroEquipmentMutationObserverReady", detail);
        return true;
#endif
    }

    void HeroEquipmentMutationObserver::Shutdown() noexcept
    {
        if (!patch_.Shutdown())
        {
            diagnostics_.Log(
                "Hook: Hero equipment patch was not removed because target ownership changed.");
            return;
        }
        SetEventSink(nullptr, nullptr);
        if (active_ == this)
        {
            active_ = nullptr;
        }
        original_ = nullptr;
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
        return active_ == this && original_ != nullptr && patch_.IsInstalled();
    }

    bool HeroEquipmentMutationObserver::InstallDetour(
        std::uint8_t* target,
        void* replacement) noexcept
    {
        return patch_.Install(
            target,
            target,
            DisplacedBytes,
            replacement,
            DisplacedBytes);
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
