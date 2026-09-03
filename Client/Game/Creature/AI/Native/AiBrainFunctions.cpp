#include "AiBrainFunctions.h"

#include <array>
#include <cstring>

namespace fable::game::creature::ai::native
{
    bool AiBrainFunctions::ResolveUpdateSlot(
        HMODULE gameModule,
        void*** slot,
        UpdatePointer& function) noexcept
    {
        if (slot != nullptr)
        {
            *slot = nullptr;
        }
        function = nullptr;
        if (gameModule == nullptr || slot == nullptr)
        {
            return false;
        }

        constexpr std::array<std::uint8_t, 10> expectedPrefix = {
            0x56, 0x8B, 0xF1, 0x8B, 0x4E,
            0x44, 0x80, 0x79, 0x10, 0x00,
        };
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto** const vtable = reinterpret_cast<void**>(base + VtableRva);
        auto** const candidateSlot = vtable + UpdateSlot;
        const auto candidate = reinterpret_cast<UpdatePointer>(base + UpdateRva);
        bool valid = false;
        __try
        {
            valid = *candidateSlot == reinterpret_cast<void*>(candidate) &&
                std::memcmp(
                    reinterpret_cast<const void*>(candidate),
                    expectedPrefix.data(),
                    expectedPrefix.size()) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid)
        {
            return false;
        }

        *slot = candidateSlot;
        function = candidate;
        return true;
    }

    bool AiBrainFunctions::ResolveStateGroupDispatcher(
        HMODULE gameModule,
        void** target,
        StateGroupDecisionPointer& function) noexcept
    {
        if (target != nullptr)
        {
            *target = nullptr;
        }
        function = nullptr;
        if (gameModule == nullptr || target == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            base + StateGroupDispatcherRva);
        const std::uint32_t expectedGlobal = static_cast<std::uint32_t>(
            base + StateGroupGlobalRva);
        std::uint32_t referencedGlobal = 0;
        std::int32_t relativeArbitration = 0;
        bool valid = false;
        __try
        {
            // Current native bytes:
            //   cmp byte ptr [global], 0; jne +5; xor al, al; ret 8
            //   mov ecx, [ecx+1e0]; jmp state-group-arbitration
            // Validate the complete branch and its tail-jump destination,
            // while the hook itself displaces only the first complete
            // 7-byte instruction.
            std::memcpy(&referencedGlobal, candidate + 2, sizeof(referencedGlobal));
            std::memcpy(
                &relativeArbitration,
                candidate + 21,
                sizeof(relativeArbitration));
            constexpr std::array<std::uint8_t, 21> expectedTail = {
                0x80, 0x3D, 0, 0, 0, 0, 0x00,
                0x75, 0x05, 0x32, 0xC0, 0xC2, 0x08, 0x00,
                0x8B, 0x89, 0xE0, 0x01, 0x00, 0x00, 0xE9};
            const auto actualArbitration = reinterpret_cast<std::uintptr_t>(
                candidate + 25 + relativeArbitration);
            valid = referencedGlobal == expectedGlobal &&
                actualArbitration == base + StateGroupArbitrationRva &&
                std::memcmp(candidate, expectedTail.data(), 2) == 0 &&
                candidate[6] == expectedTail[6] &&
                std::memcmp(candidate + 7, expectedTail.data() + 7, 14) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        if (!valid)
        {
            return false;
        }

        *target = candidate;
        function = reinterpret_cast<StateGroupDecisionPointer>(candidate);
        return true;
    }
}
