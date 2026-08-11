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
}
