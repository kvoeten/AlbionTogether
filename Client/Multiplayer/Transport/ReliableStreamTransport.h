#pragma once

#include "Multiplayer/Transport/ReliableStream.h"
#include "Multiplayer/Transport/TransportMessage.h"

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fable::multiplayer
{
    enum class ReliableReceiveResult : std::uint8_t
    {
        Rejected = 0,
        Accepted,
        Duplicate,
        Backpressured,
    };

    // Per-connection reliable stream state. A caller owns endpoint/session
    // validation; this class owns only bounded stream ordering and fairness.
    class ReliableStreamTransport final
    {
    public:
        static constexpr std::size_t TotalQueueLimit = 512;
        static constexpr std::size_t PerStreamQueueLimit = 64;
        static constexpr std::size_t StreamMetadataLimit = 256;
        // A bounded per-stream send window keeps independent streams moving
        // while retaining a small, deterministic amount of in-flight state.
        static constexpr std::size_t ReliableWindowSize = 8;
        static constexpr std::size_t MaximumFragmentCount = 2;
        static constexpr std::size_t ReliableFragmentHeaderBytes = 24;
        static constexpr std::uint64_t ReassemblyTimeoutMilliseconds = 10'000;
        static constexpr std::size_t MaximumFragmentPayloadBytes =
            protocol::MaximumDatagramBytes - protocol::PacketHeaderBytes -
            ReliableFragmentHeaderBytes;

        [[nodiscard]] bool CanEnqueue(ReliableStreamId streamId) const;
        [[nodiscard]] bool CanEnqueueMessage(
            ReliableStreamId streamId,
            protocol::PacketType type,
            const std::uint8_t* payload,
            std::size_t payloadSize) const;
        [[nodiscard]] static bool IsMessageValid(
            ReliableStreamId streamId,
            protocol::PacketType type,
            const std::uint8_t* payload,
            std::size_t payloadSize) noexcept;
        bool Enqueue(
            ReliableStreamId streamId,
            protocol::PacketType type,
            std::uint64_t sourceActorId,
            const std::uint8_t* payload,
            std::size_t payloadSize);

        [[nodiscard]] bool AcceptAcknowledgement(
            ReliableStreamId streamId,
            std::uint64_t streamIncarnation,
            std::uint32_t sequence);
        [[nodiscard]] ReliableReceiveResult AcceptIncoming(
            TransportMessage message);
        // A fragmented logical message is ACKed only after its final
        // fragment has been accepted. This keeps partial reassemblies
        // recoverable if they expire before the sender completes them.
        [[nodiscard]] bool ShouldAcknowledgeLastIncoming() const noexcept
        {
            return acknowledgeLastIncoming_;
        }

        [[nodiscard]] std::vector<TransportMessage> Due(
            std::uint64_t now,
            std::uint64_t resendMilliseconds);
        [[nodiscard]] bool TryConsume(TransportMessage& message);
        [[nodiscard]] std::size_t OutboundSize() const noexcept;
        void Clear() noexcept;

    private:
        struct OutboundStream final
        {
            std::deque<TransportMessage> queue;
            std::uint64_t incarnation = 0;
            std::uint32_t nextSequence = 1;
            std::uint32_t nextLogicalSequence = 1;
            std::unordered_set<std::uint32_t> acknowledgedSequences;
            std::unordered_map<std::uint32_t, std::uint64_t> sentAt;
            std::size_t logicalQueueSize = 0;
        };

        [[nodiscard]] static std::uint32_t NextSequence(
            std::uint32_t previous) noexcept;
        [[nodiscard]] static std::size_t Count(
            const std::unordered_map<
                ReliableStreamId,
                OutboundStream>& streams) noexcept;
        [[nodiscard]] static bool IsValidStream(
            ReliableStreamId streamId) noexcept;
        [[nodiscard]] static bool ValidatePayloadStream(
            ReliableStreamId streamId,
            protocol::PacketType type,
            const std::uint8_t* payload,
            std::size_t payloadSize) noexcept;
        struct CompletedStream final
        {
            ReliableStreamId streamId = reliable_stream::Control;
            std::uint64_t incarnation = 0;
        };

        struct ReassemblyKey final
        {
            ReliableStreamId streamId = reliable_stream::Control;
            std::uint64_t incarnation = 0;
            std::uint32_t logicalSequence = 0;

            bool operator==(const ReassemblyKey& other) const noexcept
            {
                return streamId == other.streamId &&
                    incarnation == other.incarnation &&
                    logicalSequence == other.logicalSequence;
            }
        };

        struct Reassembly final
        {
            protocol::PacketType originalType =
                protocol::PacketType::PlayerActorState;
            std::uint64_t sourceActorId = 0;
            std::uint64_t connectionNonce = 0;
            std::uint16_t fragmentCount = 0;
            std::uint16_t receivedCount = 0;
            std::uint16_t receivedMask = 0;
            std::size_t totalSize = 0;
            std::uint64_t lastTouchedAt = 0;
            std::uint32_t firstSequence = 0;
            std::array<
                std::uint8_t,
                protocol::MaximumReliableMessageBytes> payload = {};
        };

        struct ReassemblyKeyHash final
        {
            std::size_t operator()(const ReassemblyKey& key) const noexcept
            {
                const std::size_t stream =
                    std::hash<ReliableStreamId>{}(key.streamId);
                const std::size_t incarnation =
                    std::hash<std::uint64_t>{}(key.incarnation);
                const std::size_t sequence =
                    std::hash<std::uint32_t>{}(key.logicalSequence);
                return stream ^ (incarnation + static_cast<std::size_t>(
                    0x9E3779B9u) + (stream << 6) + (stream >> 2)) ^
                    (sequence << 1);
            }
        };

        [[nodiscard]] bool ReclaimCompletedStream() noexcept;
        void RememberRetiredStream(CompletedStream stream) noexcept;
        [[nodiscard]] std::uint64_t AllocateIncarnation() noexcept;
        [[nodiscard]] bool AcceptFragment(TransportMessage message);
        void ExpireReassemblies(std::uint64_t now) noexcept;

        std::unordered_map<ReliableStreamId, OutboundStream> outbound_;
        std::unordered_map<ReliableStreamId, std::uint32_t> receivedSequences_;
        std::unordered_map<ReliableStreamId, std::uint64_t>
            receivedIncarnations_;
        std::unordered_map<
            ReliableStreamId,
            std::deque<TransportMessage>> inbound_;
        std::deque<ReliableStreamId> readyStreams_;
        std::unordered_set<ReliableStreamId> readyStreamSet_;
        std::deque<CompletedStream> completedStreams_;
        std::unordered_map<ReliableStreamId, std::uint64_t>
            retiredIncarnations_;
        std::deque<CompletedStream> retiredStreams_;
        std::unordered_map<ReassemblyKey, Reassembly, ReassemblyKeyHash>
            reassemblies_;
        std::uint64_t nextIncarnation_ = 0;
        bool acknowledgeLastIncoming_ = false;
    };
}
