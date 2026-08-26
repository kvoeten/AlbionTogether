#include "HeroTransformCompatibilityHooks.h"

#include "Game/HeroPawn/TransformProbe/Native/HeroTransformCompatibilityFunctions.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
    std::uintptr_t g_component11Resume = 0;
    std::uintptr_t g_missingComponent11Cleanup = 0;

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

        g_component11Resume = component11.resume;
        g_missingComponent11Cleanup = component11.missingComponentCleanup;
        active_ = this;
        if (!fractionalPatch_.Install(
                fractionalTarget,
                fractionalTarget,
                5,
                reinterpret_cast<void*>(&SafeComponent68FractionalProgress),
                native::Component68FractionalProgressFunction::ExpectedPrefix.size()) ||
            !discretePatch_.Install(
                discreteTarget,
                discreteTarget,
                5,
                reinterpret_cast<void*>(&SafeComponent68DiscreteLevel),
                native::Component68DiscreteLevelFunction::ExpectedPrefix.size()) ||
            !component11Patch_.Install(
                component11.branch,
                component11.branch,
                5,
                reinterpret_cast<void*>(&HeroUpdateComponent11GuardThunk),
                native::HeroUpdateComponent11Branch::ExpectedPrefix.size()))
        {
            const bool component11Restored = component11Patch_.Shutdown();
            const bool discreteRestored = discretePatch_.Shutdown();
            const bool fractionalRestored = fractionalPatch_.Shutdown();
            if (!component11Restored || !discreteRestored ||
                !fractionalRestored)
            {
                diagnostics_.Log(
                    "Hook: Hero transform rollback deferred because a target is owned by another hook.");
                return false;
            }
            active_ = nullptr;
            diagnostics_.Log(
                "Hook: Hero transform compatibility guard patch installation failed.");
            return false;
        }

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
        bool allRestored = true;
        allRestored = component11Patch_.Shutdown() && allRestored;
        allRestored = discretePatch_.Shutdown() && allRestored;
        allRestored = fractionalPatch_.Shutdown() && allRestored;
        if (!allRestored)
        {
            diagnostics_.Log(
                "Hook: Hero transform shutdown deferred because a target is owned by another hook.");
            return;
        }
        if (active_ == this) active_ = nullptr;
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
