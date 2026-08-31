#include "Multiplayer/Transport/ReliableStreamTransport.h"

#include <array>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <thread>

namespace
{
#pragma pack(push, 1)
    struct TestFragmentHeader final
    {
        std::uint32_t magic = 0x52474654u;
        std::uint8_t version = 1;
        std::uint8_t originalType = static_cast<std::uint8_t>(
            fable::multiplayer::protocol::PacketType::PlayerActorState);
        std::uint16_t fragmentIndex = 0;
        std::uint16_t fragmentCount = 2;
        std::uint16_t chunkSize = 0;
        std::uint32_t totalSize = 0;
        std::uint32_t logicalSequence = 1;
        std::uint32_t reserved = 0;
    };
#pragma pack(pop)

    fable::multiplayer::TransportMessage MakeTestFragment(
        const std::uint16_t index,
        const std::uint32_t sequence)
    {
        using namespace fable::multiplayer;
        constexpr std::uint64_t actorId = 42;
        constexpr std::size_t firstChunk =
            ReliableStreamTransport::MaximumFragmentPayloadBytes;
        const std::size_t chunkSize = index == 0 ? firstChunk : 1;
        TestFragmentHeader header;
        header.fragmentIndex = index;
        header.chunkSize = static_cast<std::uint16_t>(chunkSize);
        header.totalSize = static_cast<std::uint32_t>(firstChunk + 1);

        TransportMessage message;
        message.type = protocol::PacketType::ReliableFragment;
        message.sourceActorId = actorId;
        message.connectionNonce = 77;
        message.streamId = reliable_stream::Actor(actorId);
        message.streamIncarnation = 99;
        message.sequence = sequence;
        message.payloadSize = sizeof(header) + chunkSize;
        std::memcpy(message.payload.data(), &header, sizeof(header));
        std::memset(
            message.payload.data() + sizeof(header),
            static_cast<int>(index + 1),
            chunkSize);
        message.logicalMessageEnd = index == 1;
        return message;
    }
}

#define WINDOW_REQUIRE(condition)                                          \
    do                                                                     \
    {                                                                      \
        if (!(condition))                                                  \
        {                                                                  \
            std::cerr << "reliable stream window line " << __LINE__       \
                      << " failed\n";                                     \
            return 1;                                                      \
        }                                                                  \
    } while (false)

int RunReliableStreamWindowTests()
{
    using namespace fable::multiplayer;
    constexpr ReliableStreamId stream = reliable_stream::Control;
    constexpr protocol::PacketType type = protocol::PacketType::Authority;
    const std::uint8_t payload[] = {1};
    ReliableStreamTransport sender;
    ReliableStreamTransport receiver;
    for (std::uint8_t value = 1; value <= 3; ++value)
    {
        const std::uint8_t message[] = {value};
        WINDOW_REQUIRE(sender.Enqueue(
            stream, type, 1001, message, sizeof(message)));
    }
    const auto first = sender.Due(100, 100);
    WINDOW_REQUIRE(first.size() == 3);
    WINDOW_REQUIRE(first.front().payload[0] == payload[0]);
    WINDOW_REQUIRE(
        receiver.AcceptIncoming(first[1]) == ReliableReceiveResult::Rejected);
    WINDOW_REQUIRE(
        receiver.AcceptIncoming(first[0]) == ReliableReceiveResult::Accepted);
    WINDOW_REQUIRE(
        receiver.AcceptIncoming(first[1]) == ReliableReceiveResult::Accepted);
    WINDOW_REQUIRE(sender.AcceptAcknowledgement(
        stream, first[1].streamIncarnation, first[1].sequence));
    WINDOW_REQUIRE(sender.AcceptAcknowledgement(
        stream, first[0].streamIncarnation, first[0].sequence));
    WINDOW_REQUIRE(sender.Due(101, 100).empty());
    const auto retry = sender.Due(200, 100);
    WINDOW_REQUIRE(retry.size() == 1);
    WINDOW_REQUIRE(retry.front().sequence == first[2].sequence);

    ReliableStreamTransport windowed;
    for (std::uint8_t value = 1;
         value <= ReliableStreamTransport::ReliableWindowSize + 2;
         ++value)
    {
        const std::uint8_t message[] = {value};
        WINDOW_REQUIRE(windowed.Enqueue(
            stream, type, 1001, message, sizeof(message)));
    }
    const auto window = windowed.Due(300, 100);
    WINDOW_REQUIRE(window.size() ==
        ReliableStreamTransport::ReliableWindowSize);
    const std::uint32_t unsentSequence = window.front().sequence +
        static_cast<std::uint32_t>(
            ReliableStreamTransport::ReliableWindowSize);
    WINDOW_REQUIRE(!windowed.AcceptAcknowledgement(
        stream, window.front().streamIncarnation, unsentSequence));
    WINDOW_REQUIRE(windowed.AcceptAcknowledgement(
        stream, window.front().streamIncarnation, window.front().sequence));
    const auto admitted = windowed.Due(301, 100);
    WINDOW_REQUIRE(admitted.size() == 1);
    WINDOW_REQUIRE(admitted.front().sequence == unsentSequence);

    // A partial fragment must expire when ordinary reliable traffic is
    // processed, while leaving the stream recoverable from fragment zero.
    ReliableStreamTransport expiringReceiver;
    const auto staleFirst = MakeTestFragment(0, 1);
    const auto staleFinal = MakeTestFragment(1, 2);
    WINDOW_REQUIRE(expiringReceiver.AcceptIncoming(staleFirst) ==
        ReliableReceiveResult::Accepted);
    WINDOW_REQUIRE(!expiringReceiver.ShouldAcknowledgeLastIncoming());
    std::this_thread::sleep_for(std::chrono::milliseconds(
        ReliableStreamTransport::ReassemblyTimeoutMilliseconds + 10));

    TransportMessage ordinary;
    ordinary.type = protocol::PacketType::Authority;
    ordinary.sourceActorId = 1001;
    ordinary.streamId = reliable_stream::Control;
    ordinary.streamIncarnation = 1;
    ordinary.sequence = 1;
    ordinary.payloadSize = 1;
    ordinary.payload[0] = 1;
    WINDOW_REQUIRE(expiringReceiver.AcceptIncoming(ordinary) ==
        ReliableReceiveResult::Accepted);
    WINDOW_REQUIRE(expiringReceiver.AcceptIncoming(staleFirst) ==
        ReliableReceiveResult::Accepted);
    WINDOW_REQUIRE(expiringReceiver.AcceptIncoming(staleFinal) ==
        ReliableReceiveResult::Accepted);
    WINDOW_REQUIRE(expiringReceiver.ShouldAcknowledgeLastIncoming());
    (void)payload;
    return 0;
}

#undef WINDOW_REQUIRE
