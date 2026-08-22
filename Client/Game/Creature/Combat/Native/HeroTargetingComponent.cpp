#include "HeroTargetingComponent.h"

#include <array>
#include <cstring>

namespace fable::game::creature::combat::native
{
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
                vtable[SetSelectedTargetSlot] ==
                    reinterpret_cast<void*>(base + SetSelectedTargetRva) &&
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

    bool HeroTargetingComponent::AssignSelectedTarget(
        HMODULE gameModule,
        void* targetingComponent,
        void* target) noexcept
    {
        if (!Validate(gameModule, targetingComponent))
        {
            return false;
        }
        bool invoked = false;
        __try
        {
            auto** const vtable = *reinterpret_cast<void***>(
                targetingComponent);
            using SetSelectedTargetPointer =
                void(__thiscall*)(void*, void*);
            reinterpret_cast<SetSelectedTargetPointer>(
                vtable[SetSelectedTargetSlot])(
                    targetingComponent, target);
            invoked = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            invoked = false;
        }
        if (!invoked)
        {
            return false;
        }
        HeroTargetingSnapshot snapshot;
        return ReadTargets(gameModule, targetingComponent, snapshot) &&
            (target == nullptr
                ? snapshot.selected == nullptr
                : snapshot.selected == target ||
                    snapshot.candidatePrimary == target ||
                    snapshot.candidateSecondary == target);
    }

    bool HeroTargetingComponent::ClearTargets(
        HMODULE gameModule,
        void* targetingComponent) noexcept
    {
        if (!Validate(gameModule, targetingComponent))
        {
            return false;
        }
        if (!AssignSelectedTarget(
                gameModule, targetingComponent, nullptr))
        {
            return false;
        }
        HeroTargetingSnapshot snapshot;
        return ReadTargets(gameModule, targetingComponent, snapshot) &&
            snapshot.selected == nullptr &&
            snapshot.candidatePrimary == nullptr &&
            snapshot.candidateSecondary == nullptr;
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
