#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"
#include "Game/Creature/Native/CreatureConstructorFunction.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>

namespace fable::game::creature
{
    class CreatureConstructorHook final
    {
    public:
        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;

        bool IsInstalled() const noexcept;
        unsigned int ConstructionCount() const noexcept;
        DWORD FirstConstructionThread() const noexcept;
        DWORD LastConstructionThread() const noexcept;

    private:
        static void* __fastcall Observe(void* creature, void* unused);

        static CreatureConstructorHook* active_;

        core::Diagnostics diagnostics_;
        native::CreatureConstructorFunction::Pointer original_ = nullptr;
        core::hooking::InlineHook hook_;
        std::atomic_uint constructionCount_{0};
        std::atomic<DWORD> firstConstructionThread_{0};
        std::atomic<DWORD> lastConstructionThread_{0};
    };
}
