#include "HeroTargetingComponent.h"

#include <array>
#include <cstring>

namespace fable::game::creature::combat::native
{
    namespace
    {
        bool AssignWeakTarget(
            HMODULE gameModule,
            void* targetingComponent,
            std::size_t wrapperOffset,
            void* target) noexcept
        {
            if (gameModule == nullptr || targetingComponent == nullptr ||
                target == nullptr)
            {
                return false;
            }

            constexpr std::array<std::uint8_t, 11> assignPrefix = {
                0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x04,
                0x57, 0x8B, 0x7C, 0x24, 0x0C,
            };
            const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
            const auto assignAddress = reinterpret_cast<void*>(
                base + HeroTargetingComponent::AssignWeakTargetRva);
            bool assigned = false;
            __try
            {
                if (std::memcmp(
                        assignAddress,
                        assignPrefix.data(),
                        assignPrefix.size()) != 0)
                {
                    return false;
                }
                using AssignWeakTargetFunction =
                    void(__thiscall*)(void*, void*);
                reinterpret_cast<AssignWeakTargetFunction>(assignAddress)(
                    static_cast<unsigned char*>(targetingComponent) +
                        wrapperOffset,
                    target);
                assigned = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                assigned = false;
            }
            return assigned;
        }
    }

    bool HeroTargetingComponent::Validate(
        HMODULE gameModule,
        void* targetingComponent) noexcept
    {
        if (gameModule == nullptr || targetingComponent == nullptr)
        {
            return false;
        }

        constexpr std::array<std::uint8_t, 6> selectedPrefix = {
            0x56, 0x8B, 0xF1, 0x8B, 0x4E, 0x04,
        };
        constexpr std::array<std::uint8_t, 3> candidatePrefix = {
            0x81, 0xC1, 0xE8,
        };
        constexpr std::array<std::uint8_t, 3> secondaryPrefix = {
            0x81, 0xC1, 0xF0,
        };
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        bool valid = false;
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(targetingComponent);
            valid = vtable == reinterpret_cast<void**>(base + VtableRva) &&
                vtable[SelectedTargetSlot] ==
                    reinterpret_cast<void*>(base + SelectedTargetRva) &&
                vtable[CandidatePrimarySlot] ==
                    reinterpret_cast<void*>(base + CandidatePrimaryRva) &&
                vtable[CandidateSecondarySlot] ==
                    reinterpret_cast<void*>(base + CandidateSecondaryRva) &&
                std::memcmp(
                    vtable[SelectedTargetSlot],
                    selectedPrefix.data(),
                    selectedPrefix.size()) == 0 &&
                std::memcmp(
                    vtable[CandidatePrimarySlot],
                    candidatePrefix.data(),
                    candidatePrefix.size()) == 0 &&
                std::memcmp(
                    vtable[CandidateSecondarySlot],
                    secondaryPrefix.data(),
                    secondaryPrefix.size()) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        return valid;
    }

    bool HeroTargetingComponent::ReadTargets(
        HMODULE gameModule,
        void* targetingComponent,
        HeroTargetingSnapshot& snapshot) noexcept
    {
        snapshot = {};
        if (!Validate(gameModule, targetingComponent))
        {
            Inspect(targetingComponent, snapshot);
            return false;
        }

        bool read = false;
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(targetingComponent);
            snapshot.vtable = vtable;
            snapshot.selectedFunction = vtable[SelectedTargetSlot];
            snapshot.candidatePrimaryFunction = vtable[CandidatePrimarySlot];
            snapshot.candidateSecondaryFunction = vtable[CandidateSecondarySlot];
            snapshot.selected = reinterpret_cast<GetTargetPointer>(
                vtable[SelectedTargetSlot])(targetingComponent);
            snapshot.candidatePrimary = reinterpret_cast<GetTargetPointer>(
                vtable[CandidatePrimarySlot])(targetingComponent);
            snapshot.candidateSecondary = reinterpret_cast<GetTargetPointer>(
                vtable[CandidateSecondarySlot])(targetingComponent);
            read = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            snapshot = {};
            read = false;
        }
        return read;
    }

    bool HeroTargetingComponent::AssignCandidatePrimary(
        HMODULE gameModule,
        void* targetingComponent,
        void* target) noexcept
    {
        if (!Validate(gameModule, targetingComponent) || target == nullptr)
        {
            return false;
        }

        return AssignWeakTarget(
            gameModule,
            targetingComponent,
            CandidatePrimaryOffset,
            target);
    }

    bool HeroTargetingComponent::AssignSelectedTarget(
        HMODULE gameModule,
        void* targetingComponent,
        void* target) noexcept
    {
        if (!Validate(gameModule, targetingComponent))
        {
            return false;
        }
        // GetSelectedTarget's non-mode-specific branch passes this exact
        // packed weak-reference wrapper to the retail weak getter.
        return AssignWeakTarget(
            gameModule,
            targetingComponent,
            SelectedTargetOffset,
            target);
    }

    void HeroTargetingComponent::Inspect(
        void* targetingComponent,
        HeroTargetingSnapshot& snapshot) noexcept
    {
        if (targetingComponent == nullptr)
        {
            return;
        }
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(targetingComponent);
            snapshot.vtable = vtable;
            snapshot.selectedFunction = vtable[SelectedTargetSlot];
            snapshot.candidatePrimaryFunction = vtable[CandidatePrimarySlot];
            snapshot.candidateSecondaryFunction = vtable[CandidateSecondarySlot];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            snapshot = {};
        }
    }
}
