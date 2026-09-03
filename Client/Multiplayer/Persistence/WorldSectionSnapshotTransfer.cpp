#include "WorldSectionSnapshotTransfer.h"

#include "Multiplayer/Transport/UdpPeer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace
{
    constexpr std::uint64_t EmptyHash = 14695981039346656037ull;
    constexpr std::size_t SubmissionBudget = 16;

    std::uint64_t HashBytes(
        const std::uint8_t* const bytes,
        const std::size_t byteCount) noexcept
    {
        std::uint64_t hash = EmptyHash;
        for (std::size_t i = 0; i < byteCount; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    bool IsSection(
        const fable::multiplayer::protocol::WorldSection section) noexcept
    {
        using fable::multiplayer::protocol::WorldSection;
        return section == WorldSection::Regions ||
            section == WorldSection::Factions;
    }
}

namespace fable::multiplayer::persistence
{
    std::size_t WorldSectionSnapshotTransfer::Index(
        const Section section) noexcept
    {
        return section == Section::Factions ? 1u : 0u;
    }

    void WorldSectionSnapshotTransfer::Reset() noexcept
    {
        nextTransferId_ = 0;
        latestHost_ = {};
        outbound_ = {};
        publishedPeerSetRevision_ = {};
        publishedSnapshotRevision_ = {};
        inbound_ = {};
        accepted_ = {};
        staged_ = {};
    }

    void WorldSectionSnapshotTransfer::ResetGuestAuthority() noexcept
    {
        inbound_ = {};
        accepted_ = {};
        staged_ = {};
    }

    void WorldSectionSnapshotTransfer::ResetInbound(
        const Section section) noexcept
    {
        if (IsSection(section)) inbound_[Index(section)] = {};
        else inbound_ = {};
    }

    bool WorldSectionSnapshotTransfer::PublishHostPayload(
        const Section section,
        const std::uint32_t authorityEpoch,
        const std::uint64_t sessionRevision,
        const std::uint64_t snapshotRevision,
        const std::uint8_t* const bytes,
        const std::size_t byteCount,
        const std::uint64_t peerSetRevision) noexcept
    {
        if (!IsSection(section) || authorityEpoch == 0 ||
            sessionRevision == 0 || snapshotRevision == 0 || byteCount == 0 ||
            byteCount > protocol::MaximumWorldSectionSnapshotBytes ||
            byteCount > (std::numeric_limits<std::uint32_t>::max)() ||
            (byteCount != 0 && bytes == nullptr))
        {
            return false;
        }
        try
        {
            auto mutablePayload =
                std::make_shared<std::vector<std::uint8_t>>();
            mutablePayload->assign(bytes, bytes + byteCount);
            Snapshot snapshot;
            snapshot.authorityEpoch = authorityEpoch;
            snapshot.sessionRevision = sessionRevision;
            snapshot.snapshotRevision = snapshotRevision;
            snapshot.hash = HashBytes(
                mutablePayload->data(), mutablePayload->size());
            snapshot.payload = std::move(mutablePayload);
            latestHost_[Index(section)] = std::move(snapshot);
            return QueueLatest(section, peerSetRevision);
        }
        catch (...)
        {
            outbound_[Index(section)] = {};
            return false;
        }
    }

    bool WorldSectionSnapshotTransfer::QueueLatest(
        const Section section,
        const std::uint64_t peerSetRevision)
    {
        const auto index = Index(section);
        const auto& latest = latestHost_[index];
        if (!latest.payload || latest.payload->size() >
                protocol::MaximumWorldSectionSnapshotBytes)
        {
            return false;
        }
        ++nextTransferId_;
        if (nextTransferId_ == 0) ++nextTransferId_;
        Outbound next;
        next.transferId = nextTransferId_;
        next.peerSetRevision = peerSetRevision;
        next.snapshot = latest;
        next.active = true;
        outbound_[index] = std::move(next);
        return true;
    }

    WorldSectionSnapshotTransfer::SubmissionResult
        WorldSectionSnapshotTransfer::Submit(
            UdpPeer& transport,
            const Section section,
            const protocol::WorldSectionSnapshotOperation operation,
            const std::uint8_t* const chunk,
            const std::size_t chunkSize)
    {
        const auto& outbound = outbound_[Index(section)];
        if (!outbound.snapshot.payload)
        {
            return SubmissionResult::Failed;
        }
        protocol::WorldSectionSnapshotMessage message;
        message.operation = operation;
        message.section = section;
        message.authorityEpoch = outbound.snapshot.authorityEpoch;
        message.sessionRevision = outbound.snapshot.sessionRevision;
        message.snapshotRevision = outbound.snapshot.snapshotRevision;
        message.transferId = outbound.transferId;
        message.totalBytes = static_cast<std::uint32_t>(
            outbound.snapshot.payload->size());
        message.offset = outbound.offset;
        message.hash = outbound.snapshot.hash;
        message.chunk = chunk;
        message.chunkSize = chunkSize;
        std::array<std::uint8_t, protocol::MaximumReliableMessageBytes> bytes{};
        std::size_t encodedSize = 0;
        if (!protocol::EncodeWorldSectionSnapshotMessage(message,
                bytes.data(), bytes.size(), encodedSize) ||
            transport.HasFailed())
        {
            return SubmissionResult::Failed;
        }
        if (!transport.SubmitReliable(reliable_stream::Control,
                protocol::WorldSectionSnapshotPacketType,
                bytes.data(), encodedSize))
        {
            return transport.HasFailed()
                ? SubmissionResult::Failed : SubmissionResult::Deferred;
        }
        return SubmissionResult::Submitted;
    }

    bool WorldSectionSnapshotTransfer::Process(
        UdpPeer& transport,
        const bool host)
    {
        if (host)
        {
            for (std::size_t i = 0; i < SectionCount; ++i)
            {
                const Section section = i == 0
                    ? Section::Regions : Section::Factions;
                const auto& latest = latestHost_[i];
                if (latest.payload && !outbound_[i].active &&
                    (publishedPeerSetRevision_[i] !=
                            transport.PeerSetRevision() ||
                        publishedSnapshotRevision_[i] !=
                            latest.snapshotRevision) &&
                    !QueueLatest(section, transport.PeerSetRevision()))
                {
                    return false;
                }
            }
        }
        std::size_t submitted = 0;
        for (std::size_t i = 0;
             i < SectionCount && submitted < SubmissionBudget; ++i)
        {
            const Section section = i == 0
                ? Section::Regions : Section::Factions;
            auto& outbound = outbound_[i];
            while (outbound.active && submitted < SubmissionBudget)
            {
                protocol::WorldSectionSnapshotOperation operation =
                    protocol::WorldSectionSnapshotOperation::Begin;
                const std::uint8_t* chunk = nullptr;
                std::size_t chunkSize = 0;
                if (outbound.stage == OutboundStage::Chunks)
                {
                    operation = protocol::WorldSectionSnapshotOperation::Chunk;
                    chunkSize = (std::min)(
                        protocol::MaximumWorldSectionSnapshotChunkBytes(),
                        outbound.snapshot.payload->size() - outbound.offset);
                    chunk = outbound.snapshot.payload->data() + outbound.offset;
                }
                else if (outbound.stage == OutboundStage::Commit)
                {
                    operation = protocol::WorldSectionSnapshotOperation::Commit;
                }
                const auto result = Submit(
                    transport, section, operation, chunk, chunkSize);
                if (result != SubmissionResult::Submitted)
                {
                    return result == SubmissionResult::Deferred;
                }
                ++submitted;
                if (outbound.stage == OutboundStage::Begin)
                {
                    outbound.stage = outbound.snapshot.payload->empty()
                        ? OutboundStage::Commit : OutboundStage::Chunks;
                }
                else if (outbound.stage == OutboundStage::Chunks)
                {
                    outbound.offset += static_cast<std::uint32_t>(chunkSize);
                    if (outbound.offset == outbound.snapshot.payload->size())
                        outbound.stage = OutboundStage::Commit;
                }
                else
                {
                    publishedPeerSetRevision_[i] = outbound.peerSetRevision;
                    publishedSnapshotRevision_[i] =
                        outbound.snapshot.snapshotRevision;
                    outbound = {};
                }
            }
        }
        return true;
    }

    WorldSectionSnapshotTransfer::ReceiveResult
        WorldSectionSnapshotTransfer::HandleInbound(
            const TransportMessage& transport,
            const protocol::WorldSectionSnapshotMessage& message)
    {
        if (message.operation ==
            protocol::WorldSectionSnapshotOperation::Begin)
            return BeginInbound(transport, message);
        if (message.operation ==
            protocol::WorldSectionSnapshotOperation::Chunk)
            return AppendInbound(transport, message);
        return CommitInbound(transport, message);
    }

    WorldSectionSnapshotTransfer::ReceiveResult
        WorldSectionSnapshotTransfer::BeginInbound(
            const TransportMessage& transport,
            const protocol::WorldSectionSnapshotMessage& message)
    {
        const auto index = Index(message.section);
        const Fence sessionFence = accepted_[0].authorityEpoch != 0
            ? accepted_[0] : accepted_[1];
        // Session revisions are connection nonces, not ordered counters.
        // Only authority epochs may be compared here; transport admission
        // rejects packets from an old session.
        if (sessionFence.authorityEpoch != 0 &&
            message.authorityEpoch < sessionFence.authorityEpoch)
        {
            return ReceiveResult::Ignored;
        }
        if (sessionFence.authorityEpoch != message.authorityEpoch ||
            sessionFence.sessionRevision != message.sessionRevision)
        {
            inbound_ = {};
            staged_ = {};
            accepted_ = {};
            accepted_[0] = {message.authorityEpoch,
                message.sessionRevision, 0};
            accepted_[1] = accepted_[0];
        }
        auto& fence = accepted_[index];
        if (message.snapshotRevision <= fence.snapshotRevision ||
            transport.connectionNonce == 0)
        {
            return ReceiveResult::Ignored;
        }
        try
        {
            Inbound inbound;
            inbound.authorityEpoch = message.authorityEpoch;
            inbound.sessionRevision = message.sessionRevision;
            inbound.snapshotRevision = message.snapshotRevision;
            inbound.transferId = message.transferId;
            inbound.connectionNonce = transport.connectionNonce;
            inbound.hash = message.hash;
            inbound.bytes.resize(message.totalBytes);
            inbound.active = true;
            inbound_[index] = std::move(inbound);
        }
        catch (...)
        {
            inbound_[index] = {};
        }
        return ReceiveResult::Ignored;
    }

    WorldSectionSnapshotTransfer::ReceiveResult
        WorldSectionSnapshotTransfer::AppendInbound(
            const TransportMessage& transport,
            const protocol::WorldSectionSnapshotMessage& message) noexcept
    {
        auto& inbound = inbound_[Index(message.section)];
        if (!inbound.active ||
            inbound.authorityEpoch != message.authorityEpoch ||
            inbound.sessionRevision != message.sessionRevision ||
            inbound.snapshotRevision != message.snapshotRevision ||
            inbound.transferId != message.transferId ||
            inbound.connectionNonce != transport.connectionNonce ||
            inbound.hash != message.hash ||
            message.offset != inbound.receivedBytes ||
            inbound.receivedBytes > inbound.bytes.size() ||
            message.chunkSize > inbound.bytes.size() - inbound.receivedBytes)
        {
            inbound = {};
            return ReceiveResult::Ignored;
        }
        std::memcpy(inbound.bytes.data() + inbound.receivedBytes,
            message.chunk, message.chunkSize);
        inbound.receivedBytes += static_cast<std::uint32_t>(message.chunkSize);
        return ReceiveResult::Ignored;
    }

    WorldSectionSnapshotTransfer::ReceiveResult
        WorldSectionSnapshotTransfer::CommitInbound(
            const TransportMessage& transport,
            const protocol::WorldSectionSnapshotMessage& message)
    {
        const auto index = Index(message.section);
        auto& inbound = inbound_[index];
        if (!inbound.active ||
            inbound.authorityEpoch != message.authorityEpoch ||
            inbound.sessionRevision != message.sessionRevision ||
            inbound.snapshotRevision != message.snapshotRevision ||
            inbound.transferId != message.transferId ||
            inbound.connectionNonce != transport.connectionNonce ||
            inbound.hash != message.hash ||
            message.offset != inbound.bytes.size() ||
            inbound.receivedBytes != inbound.bytes.size() ||
            HashBytes(inbound.bytes.data(), inbound.bytes.size()) !=
                inbound.hash)
        {
            inbound = {};
            return ReceiveResult::Ignored;
        }
        try
        {
            auto payload = std::make_shared<const std::vector<std::uint8_t>>(
                std::move(inbound.bytes));
            Snapshot staged;
            staged.authorityEpoch = inbound.authorityEpoch;
            staged.sessionRevision = inbound.sessionRevision;
            staged.snapshotRevision = inbound.snapshotRevision;
            staged.hash = inbound.hash;
            staged.payload = std::move(payload);
            staged_[index] = std::move(staged);
            accepted_[index] = {inbound.authorityEpoch,
                inbound.sessionRevision, inbound.snapshotRevision};
            inbound = {};
            return ReceiveResult::Accepted;
        }
        catch (...)
        {
            inbound = {};
            return ReceiveResult::Ignored;
        }
    }

    bool WorldSectionSnapshotTransfer::IsGuestReady() const noexcept
    {
        return staged_[0].payload && staged_[1].payload &&
            staged_[0].authorityEpoch == staged_[1].authorityEpoch &&
            staged_[0].sessionRevision == staged_[1].sessionRevision;
    }

    bool WorldSectionSnapshotTransfer::AcquireGuestPayload(
        const Section section,
        ImmutablePayload& payload) const noexcept
    {
        payload.reset();
        if (!IsSection(section) || !IsGuestReady()) return false;
        payload = staged_[Index(section)].payload;
        return payload != nullptr;
    }

    void WorldSectionSnapshotTransfer::MarkGuestApplied(
        const Section section,
        const bool applied) noexcept
    {
        if (IsSection(section)) staged_[Index(section)].applied = applied;
    }

    bool WorldSectionSnapshotTransfer::HasCurrentSnapshot(
        const Section section,
        const bool host) const noexcept
    {
        if (!IsSection(section)) return false;
        const auto& snapshot = host
            ? latestHost_[Index(section)] : staged_[Index(section)];
        return snapshot.payload != nullptr;
    }

    std::uint64_t WorldSectionSnapshotTransfer::CurrentSnapshotRevision(
        const Section section,
        const bool host) const noexcept
    {
        if (!IsSection(section)) return 0;
        const auto& snapshot = host
            ? latestHost_[Index(section)] : staged_[Index(section)];
        return snapshot.snapshotRevision;
    }

    std::uint64_t WorldSectionSnapshotTransfer::CurrentSnapshotFingerprint(
        const Section section,
        const bool host) const noexcept
    {
        if (!IsSection(section)) return 0;
        const auto& snapshot = host
            ? latestHost_[Index(section)] : staged_[Index(section)];
        return snapshot.hash;
    }

    std::size_t WorldSectionSnapshotTransfer::CurrentSnapshotBytes(
        const Section section,
        const bool host) const noexcept
    {
        if (!IsSection(section)) return 0;
        const auto& snapshot = host
            ? latestHost_[Index(section)] : staged_[Index(section)];
        return snapshot.payload ? snapshot.payload->size() : 0;
    }
}
