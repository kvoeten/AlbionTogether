#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"

#include <Windows.h>

#include <atomic>
#include <array>
#include <cstdint>

namespace fable::game::hero_pawn::transform_probe
{
    struct HeroTransformCompatibilityThunkAccess;

    class HeroTransformCompatibilityHooks final
    {
    public:
        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        static int __fastcall SafeComponent68DiscreteLevel(
            const void* component,
            void* unused);
        static float __fastcall SafeComponent68FractionalProgress(
            const void* component,
            void* unused);
        void LogComponent68NullFallback(
            const char* helper,
            const void* component);
        void LogComponent11Skip(const void* creature);

        friend struct HeroTransformCompatibilityThunkAccess;
        static HeroTransformCompatibilityHooks* active_;

        core::Diagnostics diagnostics_ = {};
        core::hooking::InlineHook fractionalPatch_;
        core::hooking::InlineHook discretePatch_;
        core::hooking::InlineHook component11Patch_;
        std::atomic_uint component68NullFallbacksLogged_{0};
        std::atomic_uint missingComponent11SkipsLogged_{0};
        bool installed_ = false;
    };
}
