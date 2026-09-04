#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Protocol/QuestStateSnapshotMessage.h"
#include "Multiplayer/Persistence/QuestStateAuthorityService.h"
#include "Multiplayer/Transport/TransportMessage.h"
#include "Multiplayer/Transport/UdpPeer.h"
#include "Multiplayer/Transport/ReliableStreamTransport.h"
#include "Core/Target/ExecutableValidator.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
    int failures = 0;
    bool validateExecutableForQuestTest = true;

    struct GuestApplyProbe final
    {
        std::size_t calls = 0;
        std::vector<std::uint8_t> bytes;
    };

    bool ApplyGuestQuestSnapshot(
        void* const context,
        const std::uint8_t* const bytes,
        const std::size_t byteCount) noexcept
    {
        auto* const probe = static_cast<GuestApplyProbe*>(context);
        if (probe == nullptr || (byteCount != 0 && bytes == nullptr))
        {
            return false;
        }
        try
        {
            ++probe->calls;
            if (byteCount == 0)
            {
                probe->bytes.clear();
            }
            else
            {
                probe->bytes.assign(bytes, bytes + byteCount);
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void Check(const bool condition, const char* detail)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "quest state snapshot: " << detail << "\n";
        }
    }
}

namespace fable::core::target
{
    // Production bootstrap validates the retail PE before graph startup. The
    // isolated protocol test does not load that executable.
    bool ValidateFableExecutable(HMODULE, ValidationLog) noexcept
    {
        return validateExecutableForQuestTest;
    }
}

int RunQuestStateSnapshotTests()
{
    using namespace fable::multiplayer::protocol;
    const std::array<std::uint8_t, 5> source = {1, 4, 9, 16, 25};
    std::array<std::uint8_t, MaximumReliableMessageBytes> encoded = {};
    QuestStateSnapshotMessage message;
    message.operation = QuestStateSnapshotOperation::Chunk;
    message.authorityEpoch = 7;
    message.sessionRevision = 11;
    message.snapshotRevision = 13;
    message.transferId = 17;
    message.totalBytes = 5;
    message.offset = 0;
    message.hash = 0x12345678;
    message.chunk = source.data();
    message.chunkSize = source.size();
    std::size_t encodedSize = 0;
    Check(EncodeQuestStateSnapshotMessage(message, encoded.data(),
        encoded.size(), encodedSize), "chunk encodes");

    QuestStateSnapshotMessage decoded;
    Check(DecodeQuestStateSnapshotMessage(encoded.data(), encodedSize, decoded),
        "chunk decodes");
    Check(decoded.authorityEpoch == 7 && decoded.sessionRevision == 11 &&
        decoded.snapshotRevision == 13 && decoded.transferId == 17 &&
        decoded.chunkSize == source.size(), "fencing fields round trip");
    Check(decoded.chunk != nullptr && decoded.chunk[2] == 9,
        "chunk bytes round trip");

    encoded[0] = 0xFF;
    Check(!DecodeQuestStateSnapshotMessage(encoded.data(), encodedSize, decoded),
        "unknown operation rejected");
    Check(!DecodeQuestStateSnapshotMessage(encoded.data(), encodedSize - 1,
        decoded), "truncated packet rejected");

    QuestStateSnapshotMessage begin = message;
    begin.operation = QuestStateSnapshotOperation::Begin;
    begin.chunk = nullptr;
    begin.chunkSize = 0;
    begin.offset = 0;
    Check(EncodeQuestStateSnapshotMessage(begin, encoded.data(), encoded.size(),
        encodedSize), "begin encodes");
    QuestStateSnapshotMessage commit = begin;
    commit.operation = QuestStateSnapshotOperation::Commit;
    commit.offset = commit.totalBytes;
    Check(EncodeQuestStateSnapshotMessage(commit, encoded.data(), encoded.size(),
        encodedSize), "commit encodes");
    QuestStateSnapshotMessage badCommit = commit;
    badCommit.offset = badCommit.totalBytes - 1;
    Check(!EncodeQuestStateSnapshotMessage(
        badCommit, encoded.data(), encoded.size(), encodedSize),
        "commit offset must equal total bytes");
    QuestStateSnapshotMessage badChunk = message;
    badChunk.chunkSize = 0;
    Check(!EncodeQuestStateSnapshotMessage(
        badChunk, encoded.data(), encoded.size(), encodedSize),
        "chunk size zero is rejected");

    // AutoSave.qs is 307,200 bytes in the two current fixtures. Exercise a
    // complete transfer just beyond that boundary so chunking remains bounded
    // while the snapshot cap is raised above real saves.
    const std::size_t largeSize = 307'201;
    std::vector<std::uint8_t> large(largeSize, 0xA5);
    std::size_t offset = 0;
    std::size_t chunkCount = 0;
    while (offset < large.size())
    {
        const std::size_t chunkSize = (std::min)(
            MaximumQuestStateSnapshotChunkBytes(), large.size() - offset);
        QuestStateSnapshotMessage largeChunk = message;
        largeChunk.totalBytes = static_cast<std::uint32_t>(large.size());
        largeChunk.offset = static_cast<std::uint32_t>(offset);
        largeChunk.chunk = large.data() + offset;
        largeChunk.chunkSize = chunkSize;
        Check(EncodeQuestStateSnapshotMessage(
            largeChunk, encoded.data(), encoded.size(), encodedSize),
            "307201-byte snapshot chunk encodes");
        QuestStateSnapshotMessage roundTrip;
        Check(DecodeQuestStateSnapshotMessage(
            encoded.data(), encodedSize, roundTrip),
            "307201-byte snapshot chunk decodes");
        offset += chunkSize;
        ++chunkCount;
    }
    Check(offset == largeSize && chunkCount > 1,
        "307201-byte snapshot is split into multiple chunks");

    // Periodic live captures are allowed to serialize the same manager state
    // repeatedly. Only a bytewise change may advance the host revision; this
    // keeps polling from creating reliable traffic without real progression.
    {
        fable::multiplayer::UdpPeer transport;
        fable::multiplayer::persistence::QuestStateAuthorityService service;
        validateExecutableForQuestTest = false;
        service.Initialize(fable::multiplayer::PeerRole::Host, 77,
            transport, {}, 9);
        validateExecutableForQuestTest = true;
        const std::array<std::uint8_t, 4> first = {2, 4, 6, 8};
        const std::array<std::uint8_t, 4> changed = {2, 4, 6, 9};
        service.CaptureHostSerializedBytes(first.data(), first.size());
        const std::uint64_t firstRevision = service.CurrentSnapshotRevision();
        const std::uint64_t firstFingerprint =
            service.CurrentSnapshotFingerprint();
        Check(service.HasCurrentSnapshot() && firstRevision != 0,
            "first host capture creates a current snapshot");
        service.CaptureHostSerializedBytes(first.data(), first.size());
        Check(service.CurrentSnapshotRevision() == firstRevision &&
            service.CurrentSnapshotFingerprint() == firstFingerprint,
            "identical host capture does not create a new revision");
        service.CaptureHostSerializedBytes(changed.data(), changed.size());
        Check(service.CurrentSnapshotRevision() == firstRevision + 1 &&
            service.CurrentSnapshotFingerprint() != firstFingerprint,
            "changed host capture advances exactly one revision");
        service.Shutdown();
    }

    // PlayerActorState and quest snapshots have independent reliable streams.
    // A sane Begin must establish the host fence before PlayerActorState
    // identity arrives. Native Fable hooks are deliberately disabled in this
    // protocol test; GuestApplySink owns the apply boundary below.
    {
        fable::multiplayer::UdpPeer transport;
        fable::multiplayer::persistence::QuestStateAuthorityService service;
        validateExecutableForQuestTest = false;
        service.Initialize(fable::multiplayer::PeerRole::Guest, 900,
            transport, {}, 7);
        validateExecutableForQuestTest = true;
        const std::array<std::uint8_t, 1> questByte = {42};
        constexpr std::uint64_t hashOffset = 14695981039346656037ull;
        constexpr std::uint64_t hashPrime = 1099511628211ull;
        const std::uint64_t hash = (hashOffset ^ questByte[0]) * hashPrime;
        auto MakeTransport = [&](QuestStateSnapshotMessage packet,
                                 std::uint64_t source) {
            fable::multiplayer::TransportMessage result;
            result.type = PacketType::QuestStateSnapshot;
            result.sourceActorId = source;
            result.connectionNonce = 1234;
            Check(EncodeQuestStateSnapshotMessage(packet, result.payload.data(),
                result.payload.size(), result.payloadSize),
                "ordering packet encodes");
            return result;
        };
        QuestStateSnapshotMessage first;
        first.operation = QuestStateSnapshotOperation::Begin;
        first.authorityEpoch = 7;
        first.sessionRevision = 8;
        first.snapshotRevision = 1;
        first.transferId = 1;
        first.totalBytes = 1;
        first.hash = hash;
        Check(service.HandleReliableMessage(MakeTransport(first, 77)),
            "Begin before PlayerActorState is accepted");
        QuestStateSnapshotMessage next = first;
        next.operation = QuestStateSnapshotOperation::Chunk;
        next.chunk = questByte.data();
        next.chunkSize = 1;
        Check(service.HandleReliableMessage(MakeTransport(next, 77)),
            "latched host chunk is accepted");
        QuestStateSnapshotMessage done = first;
        done.operation = QuestStateSnapshotOperation::Commit;
        done.offset = 1;
        Check(service.HandleReliableMessage(MakeTransport(done, 77)),
            "latched host commit is accepted");
        Check(service.HasStagedSnapshot(),
            "quest snapshot survives PlayerActorState ordering");
        GuestApplyProbe probe;
        service.SetGuestApplySink(&ApplyGuestQuestSnapshot, &probe);
        Check(service.IsReadyForGuestWorldLoad() &&
            !service.StagedSnapshotApplied() && probe.calls == 0,
            "pre-world gate waits for delivery without applying before guest QUESTS");
        Check(service.ApplyPendingLiveProgression() && probe.calls == 1 &&
            probe.bytes.size() == 1 && probe.bytes[0] == questByte[0] &&
            service.StagedSnapshotApplied(),
            "stable-world live apply accepts the staged authoritative snapshot");
        Check(service.ApplyPendingLiveProgression() && probe.calls == 1,
            "already applied live progression is not replayed each frame");
        Check(service.ApplyAfterNativeWorldSections() && probe.calls == 2,
            "a later full save-bundle load may reapply the authoritative snapshot");
        Check(service.ApplyAfterNativeWorldSections() && probe.calls == 3,
            "repeated full save-bundle loads retain the authoritative override");
        service.SetExpectedHostActor(88);
        Check(!service.HasStagedSnapshot() &&
            service.StagedSnapshotRevision() == 0,
            "identity mismatch invalidates provisional staged quest state");
        Check(!service.IsReadyForGuestWorldLoad(),
            "invalidated provisional quest state cannot apply");
        service.Shutdown();
    }
    Check(fable::multiplayer::ReliableStreamTransport::IsMessageValid(
        fable::multiplayer::reliable_stream::Control,
        PacketType::QuestStateSnapshot, encoded.data(), encodedSize),
        "quest snapshot uses bounded reliable control stream");

    return failures;
}
