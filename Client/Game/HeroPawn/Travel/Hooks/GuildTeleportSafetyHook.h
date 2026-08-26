#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"

#include <Windows.h>

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
        void LogFallback(unsigned int kind) noexcept;

        friend struct GuildTeleportSafetyThunkAccess;
        static GuildTeleportSafetyHook* active_;

        core::Diagnostics diagnostics_ = {};
        core::hooking::CodePatch currentEntry_;
        core::hooking::CodePatch forwardRotation_;
        core::hooking::CodePatch backwardRotation_;
        std::atomic_uint fallbackEventsLogged_{0};
        bool installed_ = false;
    };
}
