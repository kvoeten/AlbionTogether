#pragma once

#include "Multiplayer/Protocol/SessionTime.h"

#include <cstdint>
#include <limits>

namespace fable::multiplayer::protocol::equipment_transition_timing
{
    inline constexpr std::uint32_t DefaultTransitionDurationMilliseconds =
        1'000;
    inline constexpr std::uint32_t DefaultAttachmentNotifyOffsetMilliseconds =
        200;
    inline constexpr std::uint32_t MaximumFutureLeadMilliseconds = 5'000;
    inline constexpr std::uint32_t MaximumProjectionAgeMilliseconds = 60'000;

    enum class Phase : std::uint8_t
    {
        Invalid = 0,
        Future,
        Active,
        Expired,
    };

    [[nodiscard]] constexpr bool HasValidMetadata(
        std::uint64_t actionId,
        SessionTimeMs startedAt,
        std::uint32_t animationId,
        std::uint32_t durationMs,
        std::uint32_t attachmentNotifyOffsetMs) noexcept
    {
        return actionId != 0 && startedAt != SessionTimeUnset &&
            animationId != 0 && durationMs != 0 &&
            attachmentNotifyOffsetMs <= durationMs;
    }

    [[nodiscard]] constexpr Phase Evaluate(
        SessionTimeMs now,
        SessionTimeMs startedAt,
        std::uint32_t durationMs) noexcept
    {
        if (now == SessionTimeUnset || startedAt == SessionTimeUnset ||
            durationMs == 0)
        {
            return Phase::Invalid;
        }
        if (!IsSessionTimeAtOrAfter(now, startedAt))
        {
            return Phase::Future;
        }
        const auto age = static_cast<SessionTimeMs>(now - startedAt);
        return age <= durationMs ? Phase::Active : Phase::Expired;
    }

    [[nodiscard]] constexpr Phase EvaluateLocal(
        std::uint64_t now,
        std::uint64_t startedAt,
        std::uint32_t durationMs) noexcept
    {
        if (now == 0 || startedAt == 0 || durationMs == 0)
        {
            return Phase::Invalid;
        }
        if (now < startedAt)
        {
            return Phase::Future;
        }
        return now - startedAt <= durationMs
            ? Phase::Active
            : Phase::Expired;
    }

    // Projects a session timestamp to the receiving monotonic clock. Future
    // starts are preserved for a short bounded lead; implausibly old/future
    // records fail closed so they cannot replay stale native events.
    [[nodiscard]] constexpr bool ProjectStartToLocal(
        std::uint64_t localNow,
        SessionTimeMs sessionNow,
        SessionTimeMs startedAt,
        std::uint64_t& localStart) noexcept
    {
        localStart = 0;
        if (localNow == 0 || sessionNow == SessionTimeUnset ||
            startedAt == SessionTimeUnset)
        {
            return false;
        }
        if (IsSessionTimeAtOrAfter(sessionNow, startedAt))
        {
            const auto age = static_cast<SessionTimeMs>(
                sessionNow - startedAt);
            if (age > MaximumProjectionAgeMilliseconds || localNow < age)
            {
                return false;
            }
            localStart = localNow - age;
            return true;
        }
        const auto lead = static_cast<SessionTimeMs>(startedAt - sessionNow);
        if (lead > MaximumFutureLeadMilliseconds ||
            localNow > (std::numeric_limits<std::uint64_t>::max)() - lead)
        {
            return false;
        }
        localStart = localNow + lead;
        return true;
    }
}
