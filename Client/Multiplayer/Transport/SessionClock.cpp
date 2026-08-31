#include "SessionClock.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>

namespace
{
    constexpr std::uint32_t kSessionClockMagic = 0x4B4C4354u;
    constexpr std::uint8_t kSessionClockVersion = 1;

#pragma pack(push, 1)
    struct SessionClockWire final
    {
        std::uint32_t magic = kSessionClockMagic;
        std::uint8_t version = kSessionClockVersion;
        std::uint8_t kind = 0;
        std::uint16_t reserved = 0;
        std::uint32_t sequence = 0;
        std::uint64_t guestSend = 0;
        std::uint64_t hostReceive = 0;
        std::uint64_t hostSend = 0;
    };
#pragma pack(pop)

    static_assert(sizeof(SessionClockWire) ==
        fable::multiplayer::SessionClockRecordBytes);
    static_assert(std::is_trivially_copyable_v<SessionClockWire>);

    std::int64_t Difference(
        const std::uint64_t later,
        const std::uint64_t earlier) noexcept
    {
        // Clock samples are necessarily much less than 2^63 milliseconds
        // apart. Unsigned subtraction preserves the intended result across a
        // monotonic tick counter wrap before conversion to signed space.
        return static_cast<std::int64_t>(later - earlier);
    }
}

namespace fable::multiplayer
{
    bool EncodeSessionClockProbe(
        const std::uint32_t sequence,
        const std::uint64_t guestSend,
        std::array<std::uint8_t, SessionClockRecordBytes>& bytes) noexcept
    {
        if (sequence == 0 || guestSend == 0)
        {
            return false;
        }
        SessionClockWire wire;
        wire.kind = static_cast<std::uint8_t>(SessionClockRecordKind::Probe);
        wire.sequence = sequence;
        wire.guestSend = guestSend;
        std::memcpy(bytes.data(), &wire, sizeof(wire));
        return true;
    }

    bool EncodeSessionClockResponse(
        const SessionClockRecord& record,
        std::array<std::uint8_t, SessionClockRecordBytes>& bytes) noexcept
    {
        if (record.kind != SessionClockRecordKind::Response ||
            record.sequence == 0 || record.guestSend == 0 ||
            record.hostReceive == 0 || record.hostSend == 0 ||
            record.hostSend < record.hostReceive)
        {
            return false;
        }
        SessionClockWire wire;
        wire.kind = static_cast<std::uint8_t>(record.kind);
        wire.sequence = record.sequence;
        wire.guestSend = record.guestSend;
        wire.hostReceive = record.hostReceive;
        wire.hostSend = record.hostSend;
        std::memcpy(bytes.data(), &wire, sizeof(wire));
        return true;
    }

    bool DecodeSessionClock(
        const std::uint8_t* bytes,
        const std::size_t byteCount,
        SessionClockRecord& record) noexcept
    {
        record = {};
        if (bytes == nullptr || byteCount != sizeof(SessionClockWire))
        {
            return false;
        }
        SessionClockWire wire;
        std::memcpy(&wire, bytes, sizeof(wire));
        const auto kind = static_cast<SessionClockRecordKind>(wire.kind);
        if (wire.magic != kSessionClockMagic ||
            wire.version != kSessionClockVersion || wire.reserved != 0 ||
            wire.sequence == 0 || wire.guestSend == 0 ||
            (kind != SessionClockRecordKind::Probe &&
                kind != SessionClockRecordKind::Response) ||
            (kind == SessionClockRecordKind::Probe &&
                (wire.hostReceive != 0 || wire.hostSend != 0)) ||
            (kind == SessionClockRecordKind::Response &&
                (wire.hostReceive == 0 || wire.hostSend == 0 ||
                    wire.hostSend < wire.hostReceive)))
        {
            return false;
        }
        record.kind = kind;
        record.sequence = wire.sequence;
        record.guestSend = wire.guestSend;
        record.hostReceive = wire.hostReceive;
        record.hostSend = wire.hostSend;
        return true;
    }

    void SessionClockSynchronizer::Reset() noexcept
    {
        pending_.clear();
        pendingOrder_.clear();
        nextSequence_ = 1;
        minimumRoundTripMilliseconds_ =
            (std::numeric_limits<std::uint64_t>::max)();
        offsetMilliseconds_ = 0;
        synchronized_ = false;
    }

    std::uint32_t SessionClockSynchronizer::NextSequence(
        const std::uint32_t previous) noexcept
    {
        return previous == (std::numeric_limits<std::uint32_t>::max)()
            ? 1u
            : previous + 1u;
    }

    std::int64_t SessionClockSynchronizer::ClampOffset(
        const std::int64_t value) noexcept
    {
        return std::clamp(
            value, -MaximumOffsetMilliseconds, MaximumOffsetMilliseconds);
    }

    bool SessionClockSynchronizer::BeginProbe(
        const std::uint64_t localNow,
        Probe& probe) noexcept
    {
        probe = {};
        if (localNow == 0)
        {
            return false;
        }
        while (!pendingOrder_.empty())
        {
            const std::uint32_t sequence = pendingOrder_.front();
            const auto pending = pending_.find(sequence);
            if (pending == pending_.end())
            {
                pendingOrder_.pop_front();
                continue;
            }
            const std::int64_t age = Difference(localNow, pending->second);
            if (age < 0 || static_cast<std::uint64_t>(age) <=
                    ProbeTimeoutMilliseconds)
            {
                break;
            }
            pending_.erase(pending);
            pendingOrder_.pop_front();
        }
        if (pending_.size() >= MaximumPendingProbes)
        {
            return false;
        }
        const std::uint32_t sequence = nextSequence_;
        nextSequence_ = NextSequence(nextSequence_);
        pending_.emplace(sequence, localNow);
        pendingOrder_.push_back(sequence);
        probe.sequence = sequence;
        probe.guestSend = localNow;
        return true;
    }

    bool SessionClockSynchronizer::AcceptResponse(
        const SessionClockRecord& response,
        const std::uint64_t localReceiveNow) noexcept
    {
        if (response.kind != SessionClockRecordKind::Response ||
            localReceiveNow == 0 || response.hostSend < response.hostReceive)
        {
            return false;
        }
        const auto pending = pending_.find(response.sequence);
        if (pending == pending_.end() || pending->second != response.guestSend)
        {
            return false;
        }
        const std::uint64_t guestSend = pending->second;
        pending_.erase(pending);
        pendingOrder_.erase(
            std::remove(
                pendingOrder_.begin(), pendingOrder_.end(), response.sequence),
            pendingOrder_.end());

        const std::int64_t roundTripSigned =
            Difference(localReceiveNow, guestSend) -
            Difference(response.hostSend, response.hostReceive);
        if (roundTripSigned < 0 ||
            static_cast<std::uint64_t>(roundTripSigned) >
                MaximumRoundTripMilliseconds)
        {
            return false;
        }
        const std::int64_t sampleOffset = ClampOffset(
            (Difference(response.hostReceive, guestSend) +
                Difference(response.hostSend, localReceiveNow)) / 2);
        const auto roundTrip = static_cast<std::uint64_t>(roundTripSigned);
        if (minimumRoundTripMilliseconds_ ==
                (std::numeric_limits<std::uint64_t>::max)())
        {
            minimumRoundTripMilliseconds_ = roundTrip;
        }
        else
        {
            minimumRoundTripMilliseconds_ = std::min(
                minimumRoundTripMilliseconds_, roundTrip);
            if (roundTrip > minimumRoundTripMilliseconds_ +
                    OutlierRoundTripAllowanceMilliseconds)
            {
                return false;
            }
        }
        if (!synchronized_)
        {
            offsetMilliseconds_ = sampleOffset;
            synchronized_ = true;
        }
        else
        {
            // Alpha = 1/8 gives stable session timestamps while converging
            // promptly after a reconnect or a real clock-path change.
            const std::int64_t delta = sampleOffset - offsetMilliseconds_;
            const std::int64_t roundedStep = delta >= 0
                ? (delta + 4) / 8
                : (delta - 4) / 8;
            offsetMilliseconds_ += roundedStep;
            offsetMilliseconds_ = ClampOffset(offsetMilliseconds_);
        }
        return true;
    }

    std::uint64_t SessionClockSynchronizer::SessionTimeMilliseconds(
        const std::uint64_t localNow) const noexcept
    {
        return LocalToSessionTimeMilliseconds(localNow);
    }

    std::uint64_t SessionClockSynchronizer::LocalToSessionTimeMilliseconds(
        const std::uint64_t localTick) const noexcept
    {
        if (!synchronized_ || offsetMilliseconds_ == 0)
        {
            return localTick;
        }
        if (offsetMilliseconds_ > 0 &&
            localTick > (std::numeric_limits<std::uint64_t>::max)() -
                static_cast<std::uint64_t>(offsetMilliseconds_))
        {
            return (std::numeric_limits<std::uint64_t>::max)();
        }
        if (offsetMilliseconds_ < 0 && localTick <
                static_cast<std::uint64_t>(-offsetMilliseconds_))
        {
            return 0;
        }
        return offsetMilliseconds_ >= 0
            ? localTick + static_cast<std::uint64_t>(offsetMilliseconds_)
            : localTick - static_cast<std::uint64_t>(-offsetMilliseconds_);
    }
}
