#pragma once

#include <cstdint>

namespace fable::game::hero_pawn::remote
{
    inline constexpr std::uint64_t
        kActivePresentationRepairCadenceMilliseconds = 500;

    // TickCount64 is monotonic during normal operation. Treat a backwards
    // value as wrap/reset so a stale cadence marker cannot suppress repair.
    [[nodiscard]] constexpr bool IsActivePresentationRepairDue(
        std::uint64_t now,
        std::uint64_t lastRepairAt) noexcept
    {
        return lastRepairAt == 0 || now < lastRepairAt ||
            now - lastRepairAt >=
                kActivePresentationRepairCadenceMilliseconds;
    }
}
