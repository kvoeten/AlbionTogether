#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <cstdint>

namespace fable::game::hero_pawn::combat
{
    // Owns the native CTCCreatureModeManager source used by Fable while the
    // Hero holds a ranged weapon drawn. Existing replicated velocity then
    // drives the retail bow strafe locomotion instead of ordinary running.
    class RemoteHeroRangedAimController final
    {
    public:
        void Initialize(const core::Diagnostics& diagnostics) noexcept;
        void Bind(void* nativeHero, std::uint64_t actorId) noexcept;
        [[nodiscard]] bool Begin() noexcept;
        [[nodiscard]] bool End() noexcept;
        void Unbind() noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool IsActive() const noexcept { return active_; }

    private:
        static constexpr int RangedAimModeSource = 25;

        [[nodiscard]] void* ResolveModeManager() const noexcept;

        core::Diagnostics diagnostics_ = {};
        void* nativeHero_ = nullptr;
        void* modeManager_ = nullptr;
        std::uint64_t actorId_ = 0;
        bool active_ = false;
    };
}
