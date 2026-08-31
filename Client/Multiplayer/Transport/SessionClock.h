#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <unordered_map>

namespace fable::multiplayer
{
    enum class SessionClockRecordKind : std::uint8_t
    {
        Probe = 1,
        Response = 2,
    };

    struct SessionClockRecord final
    {
        SessionClockRecordKind kind = SessionClockRecordKind::Probe;
        std::uint32_t sequence = 0;
        std::uint64_t guestSend = 0;
        std::uint64_t hostReceive = 0;
        std::uint64_t hostSend = 0;
    };

    inline constexpr std::size_t SessionClockRecordBytes = 36;

    [[nodiscard]] bool EncodeSessionClockProbe(
        std::uint32_t sequence,
        std::uint64_t guestSend,
        std::array<std::uint8_t, SessionClockRecordBytes>& bytes) noexcept;
    [[nodiscard]] bool EncodeSessionClockResponse(
        const SessionClockRecord& record,
        std::array<std::uint8_t, SessionClockRecordBytes>& bytes) noexcept;
    [[nodiscard]] bool DecodeSessionClock(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        SessionClockRecord& record) noexcept;

    // NTP-style four-timestamp estimator. Only a bounded number of probes is
    // retained, high RTT samples are rejected, and accepted offset samples
    // are smoothed to avoid applying a single scheduling spike to gameplay.
    class SessionClockSynchronizer final
    {
    public:
        static constexpr std::size_t MaximumPendingProbes = 8;
        static constexpr std::uint64_t MaximumRoundTripMilliseconds = 5'000;
        static constexpr std::uint64_t ProbeTimeoutMilliseconds =
            MaximumRoundTripMilliseconds;
        static constexpr std::uint64_t OutlierRoundTripAllowanceMilliseconds =
            250;
        static constexpr std::int64_t MaximumOffsetMilliseconds = 120'000;

        struct Probe final
        {
            std::uint32_t sequence = 0;
            std::uint64_t guestSend = 0;
        };

        void Reset() noexcept;
        [[nodiscard]] bool BeginProbe(
            std::uint64_t localNow,
            Probe& probe) noexcept;
        [[nodiscard]] bool AcceptResponse(
            const SessionClockRecord& response,
            std::uint64_t localReceiveNow) noexcept;
        [[nodiscard]] bool IsSynchronized() const noexcept
        {
            return synchronized_;
        }
        [[nodiscard]] std::int64_t OffsetMilliseconds() const noexcept
        {
            return offsetMilliseconds_;
        }
        [[nodiscard]] std::uint64_t MinimumRoundTripMilliseconds() const noexcept
        {
            return minimumRoundTripMilliseconds_ ==
                    (std::numeric_limits<std::uint64_t>::max)()
                ? 0
                : minimumRoundTripMilliseconds_;
        }
        [[nodiscard]] std::uint64_t SessionTimeMilliseconds(
            std::uint64_t localNow) const noexcept;
        [[nodiscard]] std::uint64_t LocalToSessionTimeMilliseconds(
            std::uint64_t localTick) const noexcept;

    private:
        static std::uint32_t NextSequence(std::uint32_t previous) noexcept;
        static std::int64_t ClampOffset(std::int64_t value) noexcept;

        std::unordered_map<std::uint32_t, std::uint64_t> pending_;
        std::deque<std::uint32_t> pendingOrder_;
        std::uint32_t nextSequence_ = 1;
        std::uint64_t minimumRoundTripMilliseconds_ =
            (std::numeric_limits<std::uint64_t>::max)();
        std::int64_t offsetMilliseconds_ = 0;
        bool synchronized_ = false;
    };
}
