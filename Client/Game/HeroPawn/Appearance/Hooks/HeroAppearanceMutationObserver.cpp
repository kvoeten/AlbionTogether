#include "HeroAppearanceMutationObserver.h"

#include <climits>
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
            clothingDetour_.trampoline);
        if (!InstallDetour(
                modifier,
                reinterpret_cast<void*>(
                    &HeroAppearanceMutationObserver::ObserveModifierRefresh),
                6,
                modifierDetour_))
        {
            RestoreDetour(clothingDetour_);
            originalClothingRebuild_ = nullptr;
            active_ = nullptr;
            diagnostics_.Event(
                "HeroAppearanceMutationObserverRejected",
                "modifier mutation detour installation failed after clothing hook");
            return false;
        }
        originalModifierRefresh_ = reinterpret_cast<ModifierRefresh>(
            modifierDetour_.trampoline);
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
        SetEventSink(nullptr, nullptr);
        if (active_ == this)
        {
            active_ = nullptr;
        }
        RestoreDetour(modifierDetour_);
        RestoreDetour(clothingDetour_);
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
        std::array<std::uint8_t, 6> patch = {};
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

    void HeroAppearanceMutationObserver::RestoreDetour(
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
