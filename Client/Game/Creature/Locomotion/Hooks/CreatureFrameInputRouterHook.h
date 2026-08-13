#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"

#include <Windows.h>

#include <atomic>

namespace fable::game::creature::locomotion
{
    class CreatureFrameInputRouterHook final
    {
    public:
        using FrameObserver = void(*)(void* context, void* playerCreature);

        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        bool Bind(void* sourcePlayerCreature, void* targetPhysicsNavigator);
        void Clear() noexcept;
        void SetFrameObserver(FrameObserver observer, void* context) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] bool IsBound() const noexcept;
        [[nodiscard]] unsigned int RoutedFrameCount() const noexcept;

    private:
        static bool __fastcall ObservePlayerUpdate(
            void* playerCreature,
            void* unused);

        static CreatureFrameInputRouterHook* active_;

        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        ::fable::game::creature::native::CreatureFrameFunctions::UpdateFramePointer
            original_ = nullptr;
        void** vtableSlot_ = nullptr;
        std::atomic<void*> source_{nullptr};
        std::atomic<void*> targetNavigator_{nullptr};
        std::atomic<FrameObserver> frameObserver_{nullptr};
        std::atomic<void*> frameObserverContext_{nullptr};
        std::atomic_uint routedFrameCount_{0};
    };
}
