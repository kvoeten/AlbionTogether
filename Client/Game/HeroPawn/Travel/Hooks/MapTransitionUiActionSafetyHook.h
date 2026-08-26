#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"

#include <Windows.h>

#include <atomic>

namespace fable::game::hero_pawn::travel::hooks
{
    // Fable can deliver a queued UI action while its manager is between the
    // source and destination map registries. The retail action dispatcher
    // assumes the registry lookup always returns a node and dereferences a
    // null result. Skip only that incomplete transition frame.
    class MapTransitionUiActionSafetyHook final
    {
    public:
        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        using ActionFunction = void(__thiscall*)(void*, void*);

        static void __fastcall Intercept(
            void* manager,
            void*,
            void* action);
        void LogSuppressed(
            std::uintptr_t registryState,
            unsigned int actionType) noexcept;

        static MapTransitionUiActionSafetyHook* active_;

        core::Diagnostics diagnostics_ = {};
        core::hooking::CodePatch actionPatch_;
        ActionFunction original_ = nullptr;
        std::atomic_uint suppressedEvents_{0};
    };
}
