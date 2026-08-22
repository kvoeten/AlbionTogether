#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::creature::combat::native
{
    struct HeroTargetingSnapshot final
    {
        void* vtable = nullptr;
        void* selectedFunction = nullptr;
        void* candidatePrimaryFunction = nullptr;
        void* candidateSecondaryFunction = nullptr;
        void* selected = nullptr;
        void* candidatePrimary = nullptr;
        void* candidateSecondary = nullptr;
    };

    struct HeroTargetingComponent final
    {
        using GetTargetPointer = void* (__thiscall*)(void* targetingComponent);

        static constexpr std::uintptr_t VtableRva = 0x02B112AC;
        static constexpr std::size_t SelectedTargetSlot = 35;
        static constexpr std::size_t SetSelectedTargetSlot = 36;
        static constexpr std::size_t CandidatePrimarySlot = 37;
        static constexpr std::size_t CandidateSecondarySlot = 38;
        static constexpr std::uintptr_t SelectedTargetRva = 0x01AD9450;
        static constexpr std::uintptr_t SetSelectedTargetRva = 0x01ADB090;
        static constexpr std::uintptr_t CandidatePrimaryRva = 0x01A961D6;
        static constexpr std::uintptr_t CandidateSecondaryRva = 0x01A961E1;

        [[nodiscard]] static bool Validate(
            HMODULE gameModule,
            void* targetingComponent) noexcept;
        [[nodiscard]] static bool ReadTargets(
            HMODULE gameModule,
            void* targetingComponent,
            HeroTargetingSnapshot& snapshot) noexcept;
        [[nodiscard]] static bool AssignSelectedTarget(
            HMODULE gameModule,
            void* targetingComponent,
            void* target) noexcept;
        [[nodiscard]] static bool ClearTargets(
            HMODULE gameModule,
            void* targetingComponent) noexcept;
        static void Inspect(
            void* targetingComponent,
            HeroTargetingSnapshot& snapshot) noexcept;
    };
}
