#include "Multiplayer/Transport/SessionClock.h"
#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Protocol/PlayerMovementCodec.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Transport/PeerDatagramCodec.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace
{
    int failures = 0;

    void Check(const bool condition)
    {
        if (!condition)
        {
            ++failures;
        }
    }
}

int RunSessionClockTests()
{
    using namespace fable::multiplayer;
    failures = 0;

    std::array<std::uint8_t, SessionClockRecordBytes> bytes = {};
    Check(EncodeSessionClockProbe(1, 1'000, bytes));
    SessionClockRecord record;
    Check(DecodeSessionClock(bytes.data(), bytes.size(), record));
    Check(record.kind == SessionClockRecordKind::Probe &&
        record.sequence == 1 && record.guestSend == 1'000);
    bytes[0] ^= 0xFFu;
    Check(!DecodeSessionClock(bytes.data(), bytes.size(), record));

    SessionClockSynchronizer clock;
    SessionClockSynchronizer::Probe probe;
    Check(clock.BeginProbe(1'000, probe));
    SessionClockRecord response;
    response.kind = SessionClockRecordKind::Response;
    response.sequence = probe.sequence;
    response.guestSend = probe.guestSend;
    response.hostReceive = 1'055;
    response.hostSend = 1'060;
    Check(clock.AcceptResponse(response, 1'015));
    Check(clock.IsSynchronized());
    Check(clock.OffsetMilliseconds() == 50);
    Check(clock.MinimumRoundTripMilliseconds() == 10);
    Check(clock.LocalToSessionTimeMilliseconds(2'000) == 2'050);

    Check(clock.BeginProbe(2'000, probe));
    response.sequence = probe.sequence;
    response.guestSend = probe.guestSend;
    response.hostReceive = 2'070;
    response.hostSend = 2'075;
    Check(clock.AcceptResponse(response, 2'015));
    Check(clock.OffsetMilliseconds() == 52);

    Check(clock.BeginProbe(3'000, probe));
    response.sequence = probe.sequence;
    response.guestSend = probe.guestSend;
    response.hostReceive = 3'100;
    response.hostSend = 3'105;
    Check(!clock.AcceptResponse(response, 3'500));
    Check(clock.OffsetMilliseconds() == 52);

    clock.Reset();
    Check(!clock.IsSynchronized());
    Check(clock.LocalToSessionTimeMilliseconds(4'000) == 4'000);

    // Lost responses must not permanently consume the bounded probe window.
    clock.Reset();
    for (std::size_t index = 0;
         index < SessionClockSynchronizer::MaximumPendingProbes;
         ++index)
    {
        Check(clock.BeginProbe(10'000 + index, probe));
    }
    Check(!clock.BeginProbe(10'008, probe));
    Check(clock.BeginProbe(
        10'000 + SessionClockSynchronizer::ProbeTimeoutMilliseconds + 1,
        probe));

    // The transport's explicit pre-sync fallback is representable on the
    // movement wire without being mistaken for a local-clock timestamp.
    PlayerState guestState;
    guestState.actorId = 2;
    guestState.authorityEpoch = 3;
    guestState.actorGeneration = 1;
    guestState.mapEpoch = 1;
    guestState.mapId = 100;
    guestState.sequence = 1;
    guestState.changedProperties = player_property::Movement;
    guestState.movementSampleTimeMs = protocol::SessionTimeUnset;
    std::array<std::uint8_t, protocol::MaximumDatagramBytes> datagram = {};
    std::size_t datagramSize = 0;
    Check(transport_codec::EncodePlayerPacket(
        guestState, guestState.actorId, 4, datagram, datagramSize));
    protocol::PacketView packet;
    Check(protocol::DecodePacket(datagram.data(), datagramSize, packet));
    protocol::PlayerMovementMessage movement;
    Check(protocol::DecodePlayerMovementMessage(
        packet.payload, packet.payloadSize, movement));
    Check(movement.sessionTimeMs == protocol::SessionTimeUnset);
    return failures;
}
