#include "HeroAppearanceMutationObserver.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr std::uintptr_t kClothingRebuildRva = 0x019F6A45;
    constexpr std::uintptr_t kModifierRefreshRva = 0x019B4A10;
    constexpr std::array<std::uint8_t, 6> kClothingSignature = {
        0x55, 0x8B, 0xEC, 0x51, 0x53, 0x56};
    constexpr std::array<std::uint8_t, 6> kModifierSignature = {
        0x56, 0x8B, 0xF1, 0x8A, 0x46, 0x18};
}

namespace fable::game::hero_pawn::appearance::hooks
{
    HeroAppearanceMutationObserver* HeroAppearanceMutationObserver::active_ =
        nullptr;

    bool HeroAppearanceMutationObserver::Install(
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
        auto* const clothing = module + kClothingRebuildRva;
        auto* const modifier = module + kModifierRefreshRva;
        if (std::memcmp(
                clothing,
                kClothingSignature.data(),
                kClothingSignature.size()) != 0 ||
            std::memcmp(
                modifier,
                kModifierSignature.data(),
                kModifierSignature.size()) != 0)
        {
            diagnostics_.Event(
                "HeroAppearanceMutationObserverRejected",
                "retail clothing or modifier mutation signature did not match");
            return false;
        }
        active_ = this;
        if (!InstallDetour(
                clothing,
                reinterpret_cast<void*>(
                    &HeroAppearanceMutationObserver::ObserveClothingRebuild),
                5,
                clothingDetour_))
        {
            active_ = nullptr;
            return false;
        }
        originalClothingRebuild_ = reinterpret_cast<ClothingRebuild>(
            clothingDetour_.Original());
        if (!InstallDetour(
                modifier,
                reinterpret_cast<void*>(
                    &HeroAppearanceMutationObserver::ObserveModifierRefresh),
                6,
                modifierDetour_))
        {
            const bool clothingRestored = RestoreDetour(clothingDetour_);
            if (clothingRestored)
            {
                originalClothingRebuild_ = nullptr;
                if (active_ == this) active_ = nullptr;
            }
            diagnostics_.Event(
                "HeroAppearanceMutationObserverRejected",
                "modifier mutation detour installation failed after clothing hook");
            return false;
        }
        originalModifierRefresh_ = reinterpret_cast<ModifierRefresh>(
            modifierDetour_.Original());
        char detail[224] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "clothing=%p modifier=%p clothing_rva=0x%08X modifier_rva=0x%08X",
            clothing,
            modifier,
            static_cast<unsigned int>(kClothingRebuildRva),
            static_cast<unsigned int>(kModifierRefreshRva));
        diagnostics_.Event("HeroAppearanceMutationObserverReady", detail);
        return true;
#endif
    }

    void HeroAppearanceMutationObserver::Shutdown() noexcept
    {
        bool allRestored = true;
        allRestored = RestoreDetour(modifierDetour_) && allRestored;
        allRestored = RestoreDetour(clothingDetour_) && allRestored;
        if (!allRestored)
        {
            diagnostics_.Log(
                "Hook: Hero appearance shutdown deferred because a target is owned by another hook.");
            return;
        }
        SetEventSink(nullptr, nullptr);
        if (active_ == this)
        {
            active_ = nullptr;
        }
        originalModifierRefresh_ = nullptr;
        originalClothingRebuild_ = nullptr;
        mutationCount_.store(0, std::memory_order_relaxed);
        diagnostics_ = {};
    }

    void HeroAppearanceMutationObserver::SetEventSink(
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

    bool HeroAppearanceMutationObserver::IsInstalled() const noexcept
    {
        return active_ == this && originalClothingRebuild_ != nullptr &&
            originalModifierRefresh_ != nullptr;
    }

    bool HeroAppearanceMutationObserver::InstallDetour(
        std::uint8_t* target,
        void* replacement,
        std::size_t displacedBytes,
        core::hooking::InlineHook& detour) noexcept
    {
        if (target == nullptr || replacement == nullptr ||
            displacedBytes < 5)
        {
            return false;
        }
        // The complete signatures are checked by Install before this helper
        // runs; CodePatch additionally snapshots and verifies the displaced
        // bytes before taking ownership of the target.
        return detour.Install(
            target,
            target,
            displacedBytes,
            replacement,
            displacedBytes);
    }

    bool HeroAppearanceMutationObserver::RestoreDetour(
        core::hooking::InlineHook& detour) noexcept
    {
        return detour.Shutdown();
    }

    void __fastcall HeroAppearanceMutationObserver::ObserveClothingRebuild(
        void* component,
        void*,
        void* nativeThing)
    {
        HeroAppearanceMutationObserver* const observer = active_;
        if (observer == nullptr || observer->originalClothingRebuild_ == nullptr)
        {
            return;
        }
        observer->originalClothingRebuild_(component, nativeThing);
        observer->Notify({
            HeroAppearanceMutationKind::Clothing,
            nativeThing,
            component});
    }

    std::uint8_t __fastcall
        HeroAppearanceMutationObserver::ObserveModifierRefresh(
        void* component,
        void*)
    {
        HeroAppearanceMutationObserver* const observer = active_;
        if (observer == nullptr || observer->originalModifierRefresh_ == nullptr)
        {
            return 0;
        }
        // This routine is polled by the presentation update even when there is
        // nothing to rebuild. Only the dirty transition is an appearance
        // mutation worth publishing.
        bool wasDirty = false;
        __try
        {
            wasDirty =
                (*reinterpret_cast<const std::uint8_t*>(
                    static_cast<const std::uint8_t*>(component) + 0x18) &
                    0x01u) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            wasDirty = false;
        }
        const std::uint8_t result =
            observer->originalModifierRefresh_(component);
        if (!wasDirty)
        {
            return result;
        }
        observer->Notify({
            HeroAppearanceMutationKind::AttachableModifier,
            nullptr,
            component});
        return result;
    }

    void HeroAppearanceMutationObserver::Notify(
        const HeroAppearanceMutationEvent& event) noexcept
    {
        const EventSink sink = eventSink_.load(std::memory_order_acquire);
        if (sink == nullptr)
        {
            return;
        }
        mutationCount_.fetch_add(1, std::memory_order_acq_rel);
        sink(eventSinkContext_.load(std::memory_order_acquire), event);
    }
}
