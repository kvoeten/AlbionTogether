#include "RemoteRangedWeaponOrientationHook.h"

#include <climits>
#include <cstdio>
#include <cstring>
#include <limits>

namespace
{
    constexpr std::uintptr_t ResolveTransformRva = 0x01952B80;
    constexpr std::size_t PrologueSize = 6;
    constexpr std::array<std::uint8_t, 13> BodySignature = {
        0x81, 0xEC, 0x4C, 0x01, 0x00, 0x00,
        0x53, 0x56, 0x8B, 0xF1, 0x8B, 0x4E, 0x10};

    bool ValidateTarget(
        const std::uint8_t* target) noexcept
    {
        if (target == nullptr)
        {
            return false;
        }
        __try
        {
            constexpr std::array<std::uint8_t, PrologueSize> prologue = {
                0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};
            if (std::memcmp(
                    target, prologue.data(), prologue.size()) != 0)
            {
                return false;
            }
            return std::memcmp(
                    target + PrologueSize,
                    BodySignature.data(),
                    BodySignature.size()) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace fable::game::hero_pawn::equipment::hooks
{
    RemoteRangedWeaponOrientationHook*
        RemoteRangedWeaponOrientationHook::active_ = nullptr;

    bool RemoteRangedWeaponOrientationHook::Install(
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
        if (gameModule == nullptr || (active_ != nullptr && active_ != this))
        {
            return false;
        }
        auto* const target = reinterpret_cast<std::uint8_t*>(gameModule) +
            ResolveTransformRva;
        if (!ValidateTarget(target))
        {
            diagnostics_.Event(
                "RemoteRangedWeaponOrientationHookRejected",
                "retail final attachment transform signature did not match");
            return false;
        }

        active_ = this;
        if (!InstallDetour(
                target,
                reinterpret_cast<void*>(
                    &RemoteRangedWeaponOrientationHook::
                        ResolveRemoteTransform)))
        {
            active_ = nullptr;
            return false;
        }
        original_ = reinterpret_cast<ResolveTransform>(trampoline_);

        char detail[192] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "target=%p trampoline=%p registrations=%zu scope=remote-active-ranged-final-attachment",
            target_,
            trampoline_,
            registrations_.size());
        diagnostics_.Event("RemoteRangedWeaponOrientationHookReady", detail);
        return true;
#endif
    }

    void RemoteRangedWeaponOrientationHook::Shutdown() noexcept
    {
        if (active_ == this)
        {
            active_ = nullptr;
        }
        RestoreDetour();
        original_ = nullptr;
        for (Registration& registration : registrations_)
        {
            registration.token.store(0, std::memory_order_release);
            registration.carrying.store(nullptr, std::memory_order_relaxed);
            registration.weapon.store(nullptr, std::memory_order_relaxed);
            registration.actorId.store(0, std::memory_order_relaxed);
            registration.correctionReported.store(
                false, std::memory_order_relaxed);
        }
        nextToken_.store(1, std::memory_order_relaxed);
        diagnostics_ = {};
    }

    RemoteRangedWeaponOrientationHook::RegistrationToken
        RemoteRangedWeaponOrientationHook::Register(
            void* carryingComponent,
            std::uint64_t actorId) noexcept
    {
        if (!IsInstalled() || carryingComponent == nullptr || actorId == 0)
        {
            return 0;
        }
        for (Registration& registration : registrations_)
        {
            RegistrationToken expected = 0;
            if (!registration.token.compare_exchange_strong(
                    expected,
                    ReservedToken,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed))
            {
                continue;
            }
            const RegistrationToken token = NextToken();
            registration.carrying.store(
                carryingComponent, std::memory_order_relaxed);
            registration.weapon.store(nullptr, std::memory_order_relaxed);
            registration.actorId.store(actorId, std::memory_order_relaxed);
            registration.correctionReported.store(
                false, std::memory_order_relaxed);
            registration.token.store(token, std::memory_order_release);
            return token;
        }
        return 0;
    }

    bool RemoteRangedWeaponOrientationHook::SetActiveWeapon(
        RegistrationToken token,
        void* rangedWeapon) noexcept
    {
        if (token == 0 || token == ReservedToken)
        {
            return false;
        }
        for (Registration& registration : registrations_)
        {
            RegistrationToken expected = token;
            if (!registration.token.compare_exchange_strong(
                    expected,
                    ReservedToken,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed))
            {
                continue;
            }
            registration.weapon.store(
                rangedWeapon, std::memory_order_relaxed);
            registration.correctionReported.store(
                false, std::memory_order_relaxed);
            registration.token.store(token, std::memory_order_release);
            return true;
        }
        return false;
    }

    void RemoteRangedWeaponOrientationHook::Unregister(
        RegistrationToken token) noexcept
    {
        if (token == 0 || token == ReservedToken)
        {
            return;
        }
        for (Registration& registration : registrations_)
        {
            RegistrationToken expected = token;
            if (!registration.token.compare_exchange_strong(
                    expected,
                    ReservedToken,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed))
            {
                continue;
            }
            registration.carrying.store(nullptr, std::memory_order_relaxed);
            registration.weapon.store(nullptr, std::memory_order_relaxed);
            registration.actorId.store(0, std::memory_order_relaxed);
            registration.correctionReported.store(
                false, std::memory_order_relaxed);
            registration.token.store(0, std::memory_order_release);
            return;
        }
    }

    bool RemoteRangedWeaponOrientationHook::IsInstalled() const noexcept
    {
        return active_ == this && original_ != nullptr && target_ != nullptr &&
            trampoline_ != nullptr;
    }

    bool __fastcall
        RemoteRangedWeaponOrientationHook::ResolveRemoteTransform(
            void* attachment,
            void*,
            float* position,
            NativeRightHandedSet* orientation,
            bool interpolate,
            float alpha,
            bool useSolidAttachment)
    {
        RemoteRangedWeaponOrientationHook* const hook = active_;
        if (hook == nullptr || hook->original_ == nullptr)
        {
            return false;
        }
        const bool resolved = hook->original_(
            attachment,
            position,
            orientation,
            interpolate,
            alpha,
            useSolidAttachment);
        if (!resolved || orientation == nullptr)
        {
            return resolved;
        }
        Registration* const registration =
            hook->FindAttachmentMatch(attachment);
        if (registration == nullptr)
        {
            return resolved;
        }
        __try
        {
            // CRightHandedSet stores Up followed by Forward. Both the returned
            // basis and the attachment's cached basis must change: the aiming
            // path renders from the latter after resolving its animation bone.
            orientation->forward[0] = -orientation->forward[0];
            orientation->forward[1] = -orientation->forward[1];
            orientation->forward[2] = -orientation->forward[2];
            auto* const cached = reinterpret_cast<NativeRightHandedSet*>(
                static_cast<std::uint8_t*>(attachment) + 0x30);
            cached->forward[0] = orientation->forward[0];
            cached->forward[1] = orientation->forward[1];
            cached->forward[2] = orientation->forward[2];
            hook->ReportCorrection(*registration);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            // The native result remains usable if presentation memory is
            // retired concurrently with a world transition.
        }
        return resolved;
    }

    RemoteRangedWeaponOrientationHook::Registration*
        RemoteRangedWeaponOrientationHook::FindAttachmentMatch(
            void* attachment) noexcept
    {
        if (attachment == nullptr)
        {
            return nullptr;
        }
        void* carryingComponent = nullptr;
        void* carriedThing = nullptr;
        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(
                attachment);
            carriedThing = *reinterpret_cast<void* const*>(bytes + 0x04);
            carryingComponent = *reinterpret_cast<void* const*>(bytes + 0x10);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
        return FindMatch(carryingComponent, carriedThing);
    }

    RemoteRangedWeaponOrientationHook::Registration*
        RemoteRangedWeaponOrientationHook::FindMatch(
            void* carryingComponent,
            void* carriedThing) noexcept
    {
        if (carryingComponent == nullptr || carriedThing == nullptr)
        {
            return nullptr;
        }
        for (Registration& registration : registrations_)
        {
            const RegistrationToken token = registration.token.load(
                std::memory_order_acquire);
            if (token == 0 || token == ReservedToken)
            {
                continue;
            }
            if (registration.carrying.load(std::memory_order_relaxed) ==
                    carryingComponent &&
                registration.weapon.load(std::memory_order_relaxed) ==
                    carriedThing)
            {
                return &registration;
            }
        }
        return nullptr;
    }

    RemoteRangedWeaponOrientationHook::RegistrationToken
        RemoteRangedWeaponOrientationHook::NextToken() noexcept
    {
        for (;;)
        {
            const RegistrationToken token = nextToken_.fetch_add(
                1, std::memory_order_relaxed);
            if (token != 0 && token != ReservedToken)
            {
                return token;
            }
        }
    }

    void RemoteRangedWeaponOrientationHook::ReportCorrection(
        Registration& registration) noexcept
    {
        if (registration.correctionReported.exchange(
                true, std::memory_order_relaxed))
        {
            return;
        }
        char detail[160] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu carrying=%p weapon=%p correction=yaw-180 stage=final-attachment",
            static_cast<unsigned long long>(registration.actorId.load(
                std::memory_order_relaxed)),
            registration.carrying.load(std::memory_order_relaxed),
            registration.weapon.load(std::memory_order_relaxed));
        diagnostics_.Event("MultiplayerRemoteRangedOrientationCorrected", detail);
    }

    bool RemoteRangedWeaponOrientationHook::InstallDetour(
        std::uint8_t* target,
        void* replacement) noexcept
    {
        if (target == nullptr || replacement == nullptr)
        {
            return false;
        }
        auto* const trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
            nullptr,
            PrologueSize + 5,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_EXECUTE_READWRITE));
        if (trampoline == nullptr)
        {
            return false;
        }
        std::memcpy(originalBytes_.data(), target, PrologueSize);
        std::memcpy(trampoline, target, PrologueSize);
        trampoline[PrologueSize] = 0xE9;
        const std::intptr_t resume =
            reinterpret_cast<std::intptr_t>(target + PrologueSize) -
            (reinterpret_cast<std::intptr_t>(trampoline + PrologueSize) + 5);
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
            trampoline + PrologueSize + 1,
            &resumeRelative,
            sizeof(resumeRelative));

        std::array<std::uint8_t, PrologueSize> patch = {};
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
                PrologueSize,
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            VirtualFree(trampoline, 0, MEM_RELEASE);
            return false;
        }
        std::memcpy(target, patch.data(), PrologueSize);
        FlushInstructionCache(GetCurrentProcess(), target, PrologueSize);
        FlushInstructionCache(
            GetCurrentProcess(), trampoline, PrologueSize + 5);
        DWORD discarded = 0;
        VirtualProtect(
            target, PrologueSize, previousProtection, &discarded);
        target_ = target;
        trampoline_ = trampoline;
        return true;
    }

    void RemoteRangedWeaponOrientationHook::RestoreDetour() noexcept
    {
        if (target_ != nullptr)
        {
            DWORD previousProtection = 0;
            if (VirtualProtect(
                    target_,
                    PrologueSize,
                    PAGE_EXECUTE_READWRITE,
                    &previousProtection))
            {
                std::memcpy(
                    target_, originalBytes_.data(), PrologueSize);
                FlushInstructionCache(
                    GetCurrentProcess(), target_, PrologueSize);
                DWORD discarded = 0;
                VirtualProtect(
                    target_, PrologueSize, previousProtection, &discarded);
            }
        }
        if (trampoline_ != nullptr)
        {
            VirtualFree(trampoline_, 0, MEM_RELEASE);
        }
        target_ = nullptr;
        trampoline_ = nullptr;
        originalBytes_ = {};
    }
}
