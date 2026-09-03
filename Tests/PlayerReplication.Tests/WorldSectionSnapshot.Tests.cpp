#include "Multiplayer/Protocol/WorldSectionSnapshotMessage.h"
#include "Multiplayer/Persistence/WorldSectionSnapshotTransfer.h"

#include <array>
#include <cassert>
#include <cstdint>

int RunWorldSectionSnapshotTests()
{
    using namespace fable::multiplayer::protocol;

    std::array<std::uint8_t, 32> payload{};
    for (std::size_t index = 0; index < payload.size(); ++index)
    {
        payload[index] = static_cast<std::uint8_t>(index + 1);
    }
    std::array<std::uint8_t, MaximumDatagramBytes> wire{};
    std::size_t wireBytes = 0;

    WorldSectionSnapshotMessage begin;
    begin.operation = WorldSectionSnapshotOperation::Begin;
    begin.section = WorldSection::Regions;
    begin.authorityEpoch = 3;
    begin.sessionRevision = 4;
    begin.snapshotRevision = 5;
    begin.transferId = 6;
    begin.totalBytes = static_cast<std::uint32_t>(payload.size());
    begin.hash = 7;
    assert(EncodeWorldSectionSnapshotMessage(
        begin, wire.data(), wire.size(), wireBytes));
    WorldSectionSnapshotMessage decoded;
    assert(DecodeWorldSectionSnapshotMessage(
        wire.data(), wireBytes, decoded));
    assert(decoded.operation == WorldSectionSnapshotOperation::Begin);
    assert(decoded.section == WorldSection::Regions);
    assert(decoded.totalBytes == payload.size());

    WorldSectionSnapshotMessage chunk = begin;
    chunk.operation = WorldSectionSnapshotOperation::Chunk;
    chunk.chunk = payload.data();
    chunk.chunkSize = payload.size();
    assert(EncodeWorldSectionSnapshotMessage(
        chunk, wire.data(), wire.size(), wireBytes));
    assert(DecodeWorldSectionSnapshotMessage(
        wire.data(), wireBytes, decoded));
    assert(decoded.chunkSize == payload.size());
    assert(decoded.chunk != nullptr && decoded.chunk[31] == 32);

    WorldSectionSnapshotMessage commit = begin;
    commit.operation = WorldSectionSnapshotOperation::Commit;
    commit.offset = commit.totalBytes;
    assert(EncodeWorldSectionSnapshotMessage(
        commit, wire.data(), wire.size(), wireBytes));

    begin.totalBytes = 0;
    assert(!EncodeWorldSectionSnapshotMessage(
        begin, wire.data(), wire.size(), wireBytes));
    chunk = commit;
    chunk.operation = WorldSectionSnapshotOperation::Chunk;
    chunk.offset = chunk.totalBytes;
    chunk.chunk = payload.data();
    chunk.chunkSize = 1;
    assert(!EncodeWorldSectionSnapshotMessage(
        chunk, wire.data(), wire.size(), wireBytes));
    commit.section = static_cast<WorldSection>(99);
    assert(!EncodeWorldSectionSnapshotMessage(
        commit, wire.data(), wire.size(), wireBytes));

    const auto hashBytes = [](const std::uint8_t* bytes,
        const std::size_t byteCount)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (std::size_t index = 0; index < byteCount; ++index)
        {
            hash ^= bytes[index];
            hash *= 1099511628211ull;
        }
        return hash;
    };
    fable::multiplayer::persistence::WorldSectionSnapshotTransfer transfer;
    fable::multiplayer::TransportMessage transport;
    transport.sourceActorId = 99;
    transport.connectionNonce = 4;
    const auto stage = [&](const WorldSection section,
        const std::uint64_t snapshotRevision)
    {
        WorldSectionSnapshotMessage message;
        message.section = section;
        message.authorityEpoch = 3;
        message.sessionRevision = 4;
        message.snapshotRevision = snapshotRevision;
        message.transferId = 10 + snapshotRevision;
        message.totalBytes = static_cast<std::uint32_t>(payload.size());
        message.hash = hashBytes(payload.data(), payload.size());
        assert(transfer.HandleInbound(transport, message) ==
            fable::multiplayer::persistence::WorldSectionSnapshotTransfer::
                ReceiveResult::Ignored);
        message.operation = WorldSectionSnapshotOperation::Chunk;
        message.chunk = payload.data();
        message.chunkSize = payload.size();
        assert(transfer.HandleInbound(transport, message) ==
            fable::multiplayer::persistence::WorldSectionSnapshotTransfer::
                ReceiveResult::Ignored);
        message.operation = WorldSectionSnapshotOperation::Commit;
        message.offset = message.totalBytes;
        message.chunk = nullptr;
        message.chunkSize = 0;
        assert(transfer.HandleInbound(transport, message) ==
            fable::multiplayer::persistence::WorldSectionSnapshotTransfer::
                ReceiveResult::Accepted);
    };
    stage(WorldSection::Regions, 1);
    assert(!transfer.IsGuestReady());
    stage(WorldSection::Factions, 1);
    assert(transfer.IsGuestReady());
    fable::multiplayer::persistence::WorldSectionSnapshotTransfer::
        ImmutablePayload staged;
    assert(transfer.AcquireGuestPayload(WorldSection::Regions, staged));
    assert(staged != nullptr && staged->size() == payload.size());
    assert(staged->front() == 1 && staged->back() == 32);

    return 0;
}
