#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Core/Hooking/CodePatch.h"
#include "Game/Math/Vector3.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fable::game::hero_pawn::abilities::hooks
{
    // Assassin Rush normally owns its creature's traversal. A remote Hero is
    // already driven by owner-authored movement, so only the native action's
    // animation/effects may run locally. This hook suppresses world-position
    // writes made from a remote Assassin Rush frame while leaving all other
    // physics writes and local-player Rushes untouched.
    class AssassinRushPresentationHook final
    {
    public:
        bool Install(
            HMODULE gameModule,
            const core::Diagnostics& diagnostics) noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool BindRemoteHero(
            void* nativeHero,
            std::uint64_t actorId) noexcept;
        void UnbindRemoteHero(void* nativeHero) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;

    private:
        using FrameUpdatePointer = void(__thiscall*)(void* component);
        using SetWorldPositionPointer = void(__thiscall*)(
            void* component,
            const Vector3* worldPosition);

        struct Binding final
        {
            void* nativeHero = nullptr;
            std::uint64_t actorId = 0;
        };

        static constexpr std::size_t RemoteHeroCapacity = 64;

        static void __fastcall ObserveFrameUpdate(
            void* component,
            void* unused);
        static void __fastcall ObserveControlledPosition(
            void* component,
            void* unused,
            const Vector3* worldPosition);
        static void __fastcall ObserveNavigatorPosition(
            void* component,
            void* unused,
            const Vector3* worldPosition);

        [[nodiscard]] bool IsRemoteHero(
            void* nativeHero,
            std::uint64_t& actorId) const noexcept;
        void ReportSuppressed(
            void* component,
            const Vector3* worldPosition) noexcept;

        static AssassinRushPresentationHook* active_;

        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        FrameUpdatePointer originalFrameUpdate_ = nullptr;
        SetWorldPositionPointer originalControlledPosition_ = nullptr;
        SetWorldPositionPointer originalNavigatorPosition_ = nullptr;
        core::hooking::CodePatch frameUpdatePatch_;
        core::hooking::CodePatch controlledPositionPatch_;
        core::hooking::CodePatch navigatorPositionPatch_;
        mutable SRWLOCK bindingLock_ = SRWLOCK_INIT;
        std::array<Binding, RemoteHeroCapacity> bindings_ = {};
        std::atomic_uint suppressionCount_{0};
    };
}
