#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace fable::game::hero_pawn::travel::hooks
{
    struct GuildTeleportSafetyThunkAccess;

    // Guards a retail Guild-teleport assumption that is invalid while a
    // multiplayer peer is carrying map state for more than one location.
    class GuildTeleportSafetyHook final
    {
    public:
        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        struct PatchSite final
        {
            std::uint8_t* target = nullptr;
            std::array<std::uint8_t, 7> original = {};
        };

        void LogFallback(unsigned int kind) noexcept;

        friend struct GuildTeleportSafetyThunkAccess;
        static GuildTeleportSafetyHook* active_;

        core::Diagnostics diagnostics_ = {};
        PatchSite currentEntry_;
        PatchSite forwardRotation_;
        PatchSite backwardRotation_;
        std::atomic_uint fallbackEventsLogged_{0};
        bool installed_ = false;
    };
}
