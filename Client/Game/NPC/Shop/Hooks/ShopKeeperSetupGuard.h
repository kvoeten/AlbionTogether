#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"
#include "Game/NPC/Shop/Native/ShopKeeperReadiness.h"

#include <Windows.h>

#include <atomic>
#include <mutex>

namespace fable::game::npc::shop::native
{
    // Narrow guard for CAIStateGroup_SetupWares' virtual predicate. The
    // replacement rejects a malformed/partial graph and otherwise calls the
    // retail predicate through a process-lifetime InlineHook trampoline.
    class ShopKeeperSetupGuard final
    {
    public:
        using PredicatePointer = bool(__thiscall*)(void* stateGroup);

        ShopKeeperSetupGuard() noexcept = default;

        ~ShopKeeperSetupGuard() noexcept
        {
            Shutdown();
        }

        ShopKeeperSetupGuard(const ShopKeeperSetupGuard&) = delete;
        ShopKeeperSetupGuard& operator=(const ShopKeeperSetupGuard&) = delete;

        [[nodiscard]] bool Install(
            HMODULE gameModule,
            const core::Diagnostics& diagnostics = {}) noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        static bool __fastcall Invoke(void* stateGroup, void* unused);
        static void ReportDeferred(ShopKeeperReadiness state) noexcept;

        static std::atomic<ShopKeeperSetupGuard*> active_;
        static std::mutex processMutex_;
        static core::hooking::InlineHook* processHook_;
        static std::atomic<PredicatePointer> processOriginal_;
        static HMODULE processModule_;
        static core::Diagnostics processDiagnostics_;
        static std::atomic_uint diagnosticMask_;
    };
}
