#include "PlayerCommandPollFunction.h"

#include <array>
#include <cstring>

namespace fable::game::player::input::native
{
    bool PlayerCommandPollFunction::Resolve(
        HMODULE gameModule,
        void*** slot,
        Pointer& function) noexcept
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

        constexpr std::array<std::uint8_t, 6> expectedPrefix = {
            0x53, 0x8B, 0xD9, 0x33, 0xC9, 0x56,
        };
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto** const vtable = reinterpret_cast<void**>(base + VtableRva);
        auto** const candidateSlot = vtable + VtableSlot;
        const auto candidate = reinterpret_cast<Pointer>(base + FunctionRva);
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
