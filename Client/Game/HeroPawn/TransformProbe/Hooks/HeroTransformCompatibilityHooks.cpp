#include "HeroTransformCompatibilityHooks.h"

#include "Game/HeroPawn/TransformProbe/Native/HeroTransformCompatibilityFunctions.h"

#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
    std::uintptr_t g_component11Resume = 0;
    std::uintptr_t g_missingComponent11Cleanup = 0;

    template <std::size_t Size>
    bool BuildRelativeJump(
        const std::uint8_t* target,
        const void* replacement,
        std::array<std::uint8_t, Size>& patch) noexcept
    {
        static_assert(Size >= 5, "An x86 relative jump requires five bytes.");
        const std::intptr_t displacement =
            reinterpret_cast<std::intptr_t>(replacement) -
            (reinterpret_cast<std::intptr_t>(target) + 5);
        if (displacement < INT32_MIN || displacement > INT32_MAX)
        {
            return false;
        }

        patch.fill(0x90);
        patch[0] = 0xE9;
        const std::int32_t relative = static_cast<std::int32_t>(displacement);
        std::memcpy(patch.data() + 1, &relative, sizeof(relative));
        return true;
    }

    void RestoreProtection(
        void* address,
        std::size_t size,
        DWORD protection,
        const fable::core::Diagnostics& diagnostics,
        const char* name)
    {
        DWORD discardedProtection = 0;
        if (VirtualProtect(address, size, protection, &discardedProtection))
        {
            return;
        }

        char message[256] = {};
        std::snprintf(
            message,
            std::size(message),
            "Hook: %s installed, but code protection restoration failed; error=%lu.",
            name,
            static_cast<unsigned long>(GetLastError()));
        diagnostics.Log(message);
    }
}

namespace fable::game::hero_pawn::transform_probe
{
    struct HeroTransformCompatibilityThunkAccess final
    {
        static void LogMissingComponent11Skip(const void* creature)
        {
            if (HeroTransformCompatibilityHooks::active_ != nullptr)
            {
                HeroTransformCompatibilityHooks::active_->LogComponent11Skip(
                    creature);
            }
        }
    };

    HeroTransformCompatibilityHooks* HeroTransformCompatibilityHooks::active_ = nullptr;
}

namespace
{
    void __stdcall LogMissingComponent11SkipThunk(const void* creature)
    {
        fable::game::hero_pawn::transform_probe::
            HeroTransformCompatibilityThunkAccess::LogMissingComponent11Skip(
                creature);
    }

#if defined(_M_IX86)
    __declspec(naked) void HeroUpdateComponent11GuardThunk()
    {
        __asm
        {
            test al, al
            jz missing_component

            lea eax, [ebp - 28h]
            jmp dword ptr [g_component11Resume]

        missing_component:
            pushfd
            pushad
            push dword ptr [ebp - 2Ch]
            call LogMissingComponent11SkipThunk
            popad
            popfd
            jmp dword ptr [g_missingComponent11Cleanup]
        }
    }
#endif
}

namespace fable::game::hero_pawn::transform_probe
{

    bool HeroTransformCompatibilityHooks::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        diagnostics_ = diagnostics;

#if !defined(_M_IX86)
        diagnostics_.Log(
            "Hook: Hero transform compatibility is only supported by the x86 client.");
        return false;
#else
        if (active_ != nullptr && active_ != this)
        {
            diagnostics_.Log(
                "Hook: another Hero transform compatibility hook set is already active.");
            return false;
        }

        std::uint8_t* fractionalTarget = nullptr;
        std::uint8_t* discreteTarget = nullptr;
        native::HeroUpdateComponent11Branch::Addresses component11 = {};
        if (!native::Component68FractionalProgressFunction::Resolve(
                gameModule,
                fractionalTarget) ||
            !native::Component68DiscreteLevelFunction::Resolve(
                gameModule,
                discreteTarget) ||
            !native::HeroUpdateComponent11Branch::Resolve(gameModule, component11))
        {
            diagnostics_.Log(
                "Hook: Hero transform compatibility definitions failed validation; the executable ABI drifted.");
            return false;
        }

        std::array<
            std::uint8_t,
            native::Component68FractionalProgressFunction::ExpectedPrefix.size()>
            fractionalPatch = {};
        std::array<
            std::uint8_t,
            native::Component68DiscreteLevelFunction::ExpectedPrefix.size()>
            discretePatch = {};
        std::array<
            std::uint8_t,
            native::HeroUpdateComponent11Branch::ExpectedPrefix.size()>
            component11Patch = {};
        if (!BuildRelativeJump(
                fractionalTarget,
                &SafeComponent68FractionalProgress,
                fractionalPatch) ||
            !BuildRelativeJump(
                discreteTarget,
                &SafeComponent68DiscreteLevel,
                discretePatch) ||
            !BuildRelativeJump(
                component11.branch,
                &HeroUpdateComponent11GuardThunk,
                component11Patch))
        {
            diagnostics_.Log(
                "Hook: Hero transform compatibility callbacks are outside the x86 relative-jump range.");
            return false;
        }

        DWORD fractionalProtection = 0;
        DWORD discreteProtection = 0;
        DWORD component11Protection = 0;
        if (!VirtualProtect(
                fractionalTarget,
                fractionalPatch.size(),
                PAGE_EXECUTE_READWRITE,
                &fractionalProtection))
        {
            diagnostics_.Log(
                "Hook: component 0x68 fractional-progress guard could not change code protection.");
            return false;
        }
        if (!VirtualProtect(
                discreteTarget,
                discretePatch.size(),
                PAGE_EXECUTE_READWRITE,
                &discreteProtection))
        {
            RestoreProtection(
                fractionalTarget,
                fractionalPatch.size(),
                fractionalProtection,
                diagnostics_,
                "component 0x68 fractional-progress guard");
            diagnostics_.Log(
                "Hook: component 0x68 discrete-level guard could not change code protection.");
            return false;
        }
        if (!VirtualProtect(
                component11.branch,
                component11Patch.size(),
                PAGE_EXECUTE_READWRITE,
                &component11Protection))
        {
            RestoreProtection(
                discreteTarget,
                discretePatch.size(),
                discreteProtection,
                diagnostics_,
                "component 0x68 discrete-level guard");
            RestoreProtection(
                fractionalTarget,
                fractionalPatch.size(),
                fractionalProtection,
                diagnostics_,
                "component 0x68 fractional-progress guard");
            diagnostics_.Log(
                "Hook: Hero component type 0x11 guard could not change code protection.");
            return false;
        }

        g_component11Resume = component11.resume;
        g_missingComponent11Cleanup = component11.missingComponentCleanup;
        fractionalTarget_ = fractionalTarget;
        discreteTarget_ = discreteTarget;
        component11Target_ = component11.branch;
        std::memcpy(fractionalOriginal_.data(), fractionalTarget, fractionalOriginal_.size());
        std::memcpy(discreteOriginal_.data(), discreteTarget, discreteOriginal_.size());
        std::memcpy(component11Original_.data(), component11.branch, component11Original_.size());
        active_ = this;

        std::memcpy(
            fractionalTarget,
            fractionalPatch.data(),
            fractionalPatch.size());
        std::memcpy(discreteTarget, discretePatch.data(), discretePatch.size());
        std::memcpy(
            component11.branch,
            component11Patch.data(),
            component11Patch.size());
        FlushInstructionCache(
            GetCurrentProcess(),
            fractionalTarget,
            fractionalPatch.size());
        FlushInstructionCache(
            GetCurrentProcess(),
            discreteTarget,
            discretePatch.size());
        FlushInstructionCache(
            GetCurrentProcess(),
            component11.branch,
            component11Patch.size());

        RestoreProtection(
            component11.branch,
            component11Patch.size(),
            component11Protection,
            diagnostics_,
            "Hero component type 0x11 guard");
        RestoreProtection(
            discreteTarget,
            discretePatch.size(),
            discreteProtection,
            diagnostics_,
            "component 0x68 discrete-level guard");
        RestoreProtection(
            fractionalTarget,
            fractionalPatch.size(),
            fractionalProtection,
            diagnostics_,
            "component 0x68 fractional-progress guard");

        installed_ = true;
        char detail[384] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "fractional=%p discrete=%p component11_branch=%p resume=%p cleanup=%p",
            fractionalTarget,
            discreteTarget,
            component11.branch,
            reinterpret_cast<void*>(g_component11Resume),
            reinterpret_cast<void*>(g_missingComponent11Cleanup));
        diagnostics_.Log(
            "Hook: isolated Hero transform compatibility guards installed.");
        diagnostics_.Event("HeroTransformCompatibilityReady", detail);
        return true;
#endif
    }

    void HeroTransformCompatibilityHooks::Shutdown() noexcept
    {
        auto restore = [](std::uint8_t* target, const auto& bytes) noexcept
        {
            if (target == nullptr) return;
            DWORD protection = 0;
            if (VirtualProtect(target, bytes.size(), PAGE_EXECUTE_READWRITE, &protection))
            {
                std::memcpy(target, bytes.data(), bytes.size());
                FlushInstructionCache(GetCurrentProcess(), target, bytes.size());
                DWORD discarded = 0;
                VirtualProtect(target, bytes.size(), protection, &discarded);
            }
        };
        restore(component11Target_, component11Original_);
        restore(discreteTarget_, discreteOriginal_);
        restore(fractionalTarget_, fractionalOriginal_);
        if (active_ == this) active_ = nullptr;
        fractionalTarget_ = nullptr;
        discreteTarget_ = nullptr;
        component11Target_ = nullptr;
        installed_ = false;
        g_component11Resume = 0;
        g_missingComponent11Cleanup = 0;
        diagnostics_ = {};
    }

    bool HeroTransformCompatibilityHooks::IsInstalled() const noexcept
    {
        return installed_ && active_ == this;
    }

    int __fastcall HeroTransformCompatibilityHooks::SafeComponent68DiscreteLevel(
        const void* component,
        void*)
    {
        __try
        {
            const auto* bytes = static_cast<const std::uint8_t*>(component);
            const void* dependency = *reinterpret_cast<void* const*>(bytes + 0x70);
            if (dependency == nullptr)
            {
                if (active_ != nullptr)
                {
                    active_->LogComponent68NullFallback(
                        "discrete-level helper",
                        component);
                }
                return 1;
            }

            const float divisor = *reinterpret_cast<const float*>(
                static_cast<const std::uint8_t*>(dependency) + 0x78);
            if (divisor == 0.0f)
            {
                return 1;
            }

            const float value = *reinterpret_cast<const float*>(bytes + 0x38);
            const int result = static_cast<int>((value / divisor) + 1.0f);
            return result > 1 ? result : 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (active_ != nullptr)
            {
                active_->LogComponent68NullFallback(
                    "discrete-level exception fallback",
                    component);
            }
            return 1;
        }
    }

    float __fastcall HeroTransformCompatibilityHooks::SafeComponent68FractionalProgress(
        const void* component,
        void*)
    {
        __try
        {
            const auto* bytes = static_cast<const std::uint8_t*>(component);
            const void* dependency = *reinterpret_cast<void* const*>(bytes + 0x70);
            if (dependency == nullptr)
            {
                if (active_ != nullptr)
                {
                    active_->LogComponent68NullFallback(
                        "fractional-progress helper",
                        component);
                }
                return 0.0f;
            }

            const float divisor = *reinterpret_cast<const float*>(
                static_cast<const std::uint8_t*>(dependency) + 0x78);
            if (divisor == 0.0f)
            {
                return 0.0f;
            }

            const float reciprocal = 1.0f / divisor;
            const float scaled =
                (*reinterpret_cast<const float*>(bytes + 0x38) * reciprocal) + 1.0f;
            const int whole = static_cast<int>(scaled);
            const float rawFraction = scaled - static_cast<float>(whole);
            const float fraction = rawFraction > 0.0f ? rawFraction : 0.0f;
            return reciprocal * fraction;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (active_ != nullptr)
            {
                active_->LogComponent68NullFallback(
                    "fractional-progress exception fallback",
                    component);
            }
            return 0.0f;
        }
    }

    void HeroTransformCompatibilityHooks::LogComponent68NullFallback(
        const char* helper,
        const void* component)
    {
        const unsigned int observation =
            component68NullFallbacksLogged_.fetch_add(
                1,
                std::memory_order_relaxed) + 1;
        if (observation > 16)
        {
            return;
        }

        char message[320] = {};
        std::snprintf(
            message,
            std::size(message),
            "Compatibility: component 0x68 %s used a safe fallback because dependency +0x70 was null; event=%u thread=%lu component=%p.",
            helper,
            observation,
            static_cast<unsigned long>(GetCurrentThreadId()),
            component);
        diagnostics_.Log(message);
    }

    void HeroTransformCompatibilityHooks::LogComponent11Skip(const void* creature)
    {
        const unsigned int observation =
            missingComponent11SkipsLogged_.fetch_add(
                1,
                std::memory_order_relaxed) + 1;
        if (observation > 16)
        {
            return;
        }

        char message[320] = {};
        std::snprintf(
            message,
            std::size(message),
            "Compatibility: skipped a Hero-specific update because the transformed creature has no component type 0x11; event=%u thread=%lu creature=%p.",
            observation,
            static_cast<unsigned long>(GetCurrentThreadId()),
            creature);
        diagnostics_.Log(message);
    }
}
