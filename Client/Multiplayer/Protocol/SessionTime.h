#pragma once

#include <cstdint>

namespace fable::multiplayer::protocol
{
    // Session clocks are deliberately relative to a transport/session start,
    // rather than wall-clock time.  They wrap at 2^32 milliseconds (~49 days).
    using SessionTimeMs = std::uint32_t;

    inline constexpr SessionTimeMs SessionTimeUnset = 0;
    inline constexpr SessionTimeMs SessionTimeHalfRange = 0x80000000u;

    [[nodiscard]] constexpr SessionTimeMs ToSessionTime(
        const std::uint64_t milliseconds) noexcept
    {
        const auto narrowed = static_cast<SessionTimeMs>(milliseconds);
        // Zero is reserved for "not supplied". Skipping that single value at
        // the wrap boundary keeps every live sample self-describing.
        return narrowed == SessionTimeUnset ? 1u : narrowed;
    }

    [[nodiscard]] constexpr bool IsSessionTimeSet(
        const SessionTimeMs value) noexcept
    {
        return value != SessionTimeUnset;
    }

    // Ordering is valid only when the compared timestamps are less than half
    // the uint32 range apart. Unsigned subtraction supplies the wrap-safe
    // modulo arithmetic required at the rollover boundary.
    [[nodiscard]] constexpr bool IsSessionTimeAtOrAfter(
        const SessionTimeMs value,
        const SessionTimeMs reference) noexcept
    {
        return value == reference ||
            static_cast<SessionTimeMs>(value - reference) <
                SessionTimeHalfRange;
    }

    [[nodiscard]] constexpr bool IsSessionTimeWithin(
        const SessionTimeMs value,
        const SessionTimeMs reference,
        const SessionTimeMs maximumDeltaMs) noexcept
    {
        return maximumDeltaMs < SessionTimeHalfRange &&
            IsSessionTimeAtOrAfter(value, reference) &&
            static_cast<SessionTimeMs>(value - reference) <=
                maximumDeltaMs;
    }
}
