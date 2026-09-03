#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"

#include <Windows.h>

#include <atomic>

namespace fable::game
{
    class EntityService;
}

namespace fable::game::hero_pawn::travel::hooks
{
    // Fable can deliver a queued UI action while its manager is between the
    // source and destination map registries. The retail dispatcher also keeps
    // a raw Hero pointer that remote Hero construction can overwrite. Hold
    // incomplete transition frames and repair that pointer before dispatch.
    class MapTransitionUiActionSafetyHook final
    {
    public:
        bool Install(
            HMODULE gameModule,
            EntityService& entities,
            const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        using ActionFunction = void(__thiscall*)(void*, void*);
        using SetHeroTargetFunction = void(__thiscall*)(void*, void*);

        static void __fastcall Intercept(
            void* manager,
            void*,
            void* action);
        [[nodiscard]] void* ResolveLocalHero() const noexcept;
        [[nodiscard]] bool RepairHeroTarget(
            void* manager,
            void* localHero,
            void*& previousTarget) const noexcept;
        void LogSuppressed(
            std::uintptr_t registryState,
            unsigned int actionType,
            const char* reason) noexcept;
        void LogHeroTargetRepaired(
            void* previousTarget,
            void* localHero) noexcept;

        static MapTransitionUiActionSafetyHook* active_;

        core::Diagnostics diagnostics_ = {};
        core::hooking::CodePatch actionPatch_;
        HMODULE gameModule_ = nullptr;
        EntityService* entities_ = nullptr;
        void** managerVtable_ = nullptr;
        void** registryStateSlot_ = nullptr;
        SetHeroTargetFunction setHeroTarget_ = nullptr;
        ActionFunction original_ = nullptr;
        std::atomic_uint suppressedEvents_{0};
        std::atomic_uint repairedHeroTargetEvents_{0};
    };
}
