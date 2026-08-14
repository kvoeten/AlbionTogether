#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Math/Vector3.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>

namespace fable::game::hero_pawn::appearance::hooks
{
    class RemoteHeroPresentationFactoryHook final
    {
    public:
        bool Install(
            HMODULE gameModule,
            const core::Diagnostics& diagnostics) noexcept;

        using ArmToken = std::uint64_t;

        ArmToken Arm(const game::Vector3& expectedPosition) noexcept;
        void TargetGraphic(ArmToken token, void* graphic) noexcept;
        void Cancel(ArmToken token = 0) noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept;
        [[nodiscard]] bool IsArmed() const noexcept;

    private:
        using FactoryFunction = void(__cdecl*)(
            void* command,
            void* definitionName);

        static void __cdecl CreatePresentation(
            void* command,
            void* definitionName);

        static RemoteHeroPresentationFactoryHook* active_;

        FactoryFunction original_ = nullptr;
        void* trampoline_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::atomic_uint32_t expectedX_{0};
        std::atomic_uint32_t expectedY_{0};
        std::atomic_uint32_t expectedZ_{0};
        std::atomic_uintptr_t expectedGraphic_{0};
        std::atomic_uint32_t observations_{0};
        std::atomic_uint64_t expiresAt_{0};
        std::atomic_uint64_t armToken_{0};
        std::atomic_bool armed_{false};
        bool installed_ = false;
    };
}
