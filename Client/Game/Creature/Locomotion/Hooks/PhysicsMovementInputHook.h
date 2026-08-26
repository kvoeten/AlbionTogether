#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"
#include "Game/Creature/Locomotion/Native/PhysicsMovementFunctions.h"

#include <Windows.h>

#include <atomic>

namespace fable::game::creature::locomotion
{
    class PhysicsWorldPositionMirrorHook final
    {
    public:
        bool Install(HMODULE gameModule, const core::Diagnostics& diagnostics);
        void Shutdown() noexcept;
        bool Bind(void* sourcePhysicsControlled, void* targetPhysicsNavigator);
        void Clear() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] bool IsBound() const noexcept;
        [[nodiscard]] unsigned int MirrorCount() const noexcept;

    private:
        static void __fastcall Observe(
            void* component,
            void* unused,
            const Vector3* worldPosition);

        static PhysicsWorldPositionMirrorHook* active_;

        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        native::PhysicsWorldPositionFunctions::SetWorldPositionPointer original_ = nullptr;
        core::hooking::CodePatch vtablePatch_;
        std::atomic<void*> source_{nullptr};
        std::atomic<void*> target_{nullptr};
        std::atomic_uint mirrorCount_{0};
    };
}
