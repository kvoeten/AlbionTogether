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
            std::uint32_t acknowledgedSequence = 0;
            std::uint32_t lastSentSequence = 0;
            std::uint64_t lastSentAt = 0;
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

        [[nodiscard]] bool ReclaimCompletedStream() noexcept;
        void RememberRetiredStream(CompletedStream stream) noexcept;
        [[nodiscard]] std::uint64_t AllocateIncarnation() noexcept;

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
        std::uint64_t nextIncarnation_ = 0;
    };
}
