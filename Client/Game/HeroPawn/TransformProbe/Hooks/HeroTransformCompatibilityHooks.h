#pragma once

#include "Core/Diagnostics/Diagnostics.h"

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
        std::uint8_t* fractionalTarget_ = nullptr;
        std::uint8_t* discreteTarget_ = nullptr;
        std::uint8_t* component11Target_ = nullptr;
        std::array<std::uint8_t, 6> fractionalOriginal_ = {};
        std::array<std::uint8_t, 8> discreteOriginal_ = {};
        std::array<std::uint8_t, 7> component11Original_ = {};
        std::atomic_uint component68NullFallbacksLogged_{0};
        std::atomic_uint missingComponent11SkipsLogged_{0};
        bool installed_ = false;
    };
}
