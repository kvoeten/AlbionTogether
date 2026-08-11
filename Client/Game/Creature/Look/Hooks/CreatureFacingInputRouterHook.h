#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"

#include <Windows.h>

#include <atomic>

namespace fable::game::creature::look
{
    class CreatureFacingInputRouterHook final
    {
    public:
        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        bool Bind(void* targetCreature, void* targetPhysicsNavigator);
        void Clear() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] bool IsBound() const noexcept;
        [[nodiscard]] unsigned int RoutedFacingCount() const noexcept;

    private:
        static bool __fastcall ObserveCreatureUpdate(
            void* creature,
            void* unused);

        static CreatureFacingInputRouterHook* active_;

        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        ::fable::game::creature::native::CreatureFrameFunctions::UpdateFramePointer
            original_ = nullptr;
        void** vtableSlot_ = nullptr;
        mutable SRWLOCK bindingLock_ = SRWLOCK_INIT;
        void* targetCreature_ = nullptr;
        void* targetNavigator_ = nullptr;
        std::atomic_uint routedFacingCount_{0};
    };
}
