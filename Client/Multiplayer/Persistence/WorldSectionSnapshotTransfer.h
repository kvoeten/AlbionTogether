#pragma once

#include "Multiplayer/Protocol/WorldSectionSnapshotMessage.h"
#include "Multiplayer/Transport/TransportMessage.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace fable::multiplayer
{
    class UdpPeer;
}

namespace fable::multiplayer::persistence
{
    // Owns the bounded immutable snapshot store and its reliable transfer
    // state. The authority service remains responsible for native lifecycle,
    // source admission, and reporting.
    class WorldSectionSnapshotTransfer final
    {
    public:
        using Section = protocol::WorldSection;
        using ImmutablePayload =
            std::shared_ptr<const std::vector<std::uint8_t>>;

        enum class ReceiveResult : std::uint8_t
        {
            Ignored,
            Accepted,
        };

        WorldSectionSnapshotTransfer() = default;
        WorldSectionSnapshotTransfer(
            const WorldSectionSnapshotTransfer&) = delete;
        WorldSectionSnapshotTransfer& operator=(
            const WorldSectionSnapshotTransfer&) = delete;

        void Reset() noexcept;
        void ResetGuestAuthority() noexcept;
        void ResetInbound(Section section) noexcept;

        bool PublishHostPayload(
            Section section,
            std::uint32_t authorityEpoch,
            std::uint64_t sessionRevision,
            std::uint64_t snapshotRevision,
            const std::uint8_t* bytes,
            std::size_t byteCount,
            std::uint64_t peerSetRevision) noexcept;
        bool Process(UdpPeer& transport, bool host);

        ReceiveResult HandleInbound(
            const TransportMessage& transport,
            const protocol::WorldSectionSnapshotMessage& message);

        [[nodiscard]] bool IsGuestReady() const noexcept;
        [[nodiscard]] bool AcquireGuestPayload(
            Section section,
            ImmutablePayload& payload) const noexcept;
        void MarkGuestApplied(Section section, bool applied) noexcept;
        [[nodiscard]] bool HasCurrentSnapshot(Section section,
            bool host) const noexcept;
        [[nodiscard]] std::uint64_t CurrentSnapshotRevision(
            Section section, bool host) const noexcept;
        [[nodiscard]] std::uint64_t CurrentSnapshotFingerprint(
            Section section, bool host) const noexcept;
        [[nodiscard]] std::size_t CurrentSnapshotBytes(
            Section section, bool host) const noexcept;

    private:
        static constexpr std::size_t SectionCount = 2;
        enum class OutboundStage : std::uint8_t { Begin, Chunks, Commit };
        enum class SubmissionResult : std::uint8_t
        {
            Submitted,
            Deferred,
            Failed,
        };

        struct Snapshot final
        {
            std::uint32_t authorityEpoch = 0;
            std::uint64_t sessionRevision = 0;
            std::uint64_t snapshotRevision = 0;
            std::uint64_t hash = 0;
            ImmutablePayload payload;
            bool applied = false;
        };
        struct Outbound final
        {
            OutboundStage stage = OutboundStage::Begin;
            std::uint64_t transferId = 0;
            std::uint64_t peerSetRevision = 0;
            std::uint32_t offset = 0;
            Snapshot snapshot;
            bool active = false;
        };
        struct Inbound final
        {
            std::uint32_t authorityEpoch = 0;
            std::uint64_t sessionRevision = 0;
            std::uint64_t snapshotRevision = 0;
            std::uint64_t transferId = 0;
            std::uint64_t connectionNonce = 0;
            std::uint64_t hash = 0;
            std::uint32_t receivedBytes = 0;
            bool active = false;
            std::vector<std::uint8_t> bytes;
        };
        struct Fence final
        {
            std::uint32_t authorityEpoch = 0;
            std::uint64_t sessionRevision = 0;
            std::uint64_t snapshotRevision = 0;
        };

        [[nodiscard]] static std::size_t Index(Section section) noexcept;
        bool QueueLatest(Section section, std::uint64_t peerSetRevision);
        [[nodiscard]] SubmissionResult Submit(
            UdpPeer& transport,
            Section section,
            protocol::WorldSectionSnapshotOperation operation,
            const std::uint8_t* chunk,
            std::size_t chunkSize);
        [[nodiscard]] ReceiveResult BeginInbound(
            const TransportMessage& transport,
            const protocol::WorldSectionSnapshotMessage& message);
        [[nodiscard]] ReceiveResult AppendInbound(
            const TransportMessage& transport,
            const protocol::WorldSectionSnapshotMessage& message) noexcept;
        [[nodiscard]] ReceiveResult CommitInbound(
            const TransportMessage& transport,
            const protocol::WorldSectionSnapshotMessage& message);

        std::uint64_t nextTransferId_ = 0;
        std::array<Snapshot, SectionCount> latestHost_ = {};
        std::array<Outbound, SectionCount> outbound_ = {};
        std::array<std::uint64_t, SectionCount> publishedPeerSetRevision_ = {};
        std::array<std::uint64_t, SectionCount> publishedSnapshotRevision_ = {};
        std::array<Inbound, SectionCount> inbound_ = {};
        std::array<Fence, SectionCount> accepted_ = {};
        std::array<Snapshot, SectionCount> staged_ = {};
    };
}
