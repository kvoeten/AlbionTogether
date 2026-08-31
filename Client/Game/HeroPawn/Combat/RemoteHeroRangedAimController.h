#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <cstdint>

namespace fable::game::hero_pawn::combat
{
    // Tracks the native CTCCreatureModeManager source installed by Fable's
    // accepted HeroLoadRangedWeapon action. Existing replicated velocity then
    // drives the retail bow strafe locomotion instead of ordinary running.
    //
    // The controller must not add source 25 itself: the native action already
    // does that. Adding it twice and removing it once leaves the remote Hero
    // permanently in the ranged pose after switching weapons.
    class RemoteHeroRangedAimController final
    {
    public:
        void Initialize(const core::Diagnostics& diagnostics) noexcept;
        void Bind(void* nativeHero, std::uint64_t actorId) noexcept;
        [[nodiscard]] bool TrackNativeBegin() noexcept;
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
