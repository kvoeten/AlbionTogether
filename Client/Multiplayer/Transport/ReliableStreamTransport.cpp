#include "ReliableStreamTransport.h"

#include "Multiplayer/Protocol/EntityActionMessage.h"
#include "Multiplayer/Protocol/EntityLifecycleMessage.h"
#include "Multiplayer/Protocol/EntityLowSimulationMessageCodec.h"
#include "Multiplayer/Protocol/EntityVitalsMessageCodec.h"
#include "Multiplayer/Protocol/PlayerActionMessage.h"
#include "Multiplayer/Protocol/PlayerActorStateCodec.h"
#include "Multiplayer/Protocol/CombatHitCodec.h"
#include "ConnectionNonceRegistry.h"

#include <limits>
#include <cstring>

namespace fable::multiplayer
{
    std::uint32_t ReliableStreamTransport::NextSequence(
        const std::uint32_t previous) noexcept
    {
        return previous == (std::numeric_limits<std::uint32_t>::max)()
            ? 1u
            : previous + 1u;
    }

    std::size_t ReliableStreamTransport::Count(
        const std::unordered_map<ReliableStreamId, OutboundStream>& streams)
        noexcept
    {
        std::size_t count = 0;
        for (const auto& [streamId, stream] : streams)
        {
            (void)streamId;
            count += stream.queue.size();
        }
        return count;
    }

    std::size_t ReliableStreamTransport::OutboundSize() const noexcept
    {
        return Count(outbound_);
    }

    bool ReliableStreamTransport::IsValidStream(
        const ReliableStreamId streamId) noexcept
    {
        if (streamId.kind == ReliableStreamKind::Control ||
            streamId.kind == ReliableStreamKind::World)
        {
            return streamId.subject == 0;
        }
        return (streamId.kind == ReliableStreamKind::Actor ||
                streamId.kind == ReliableStreamKind::Entity) &&
            streamId.subject != 0;
    }

    bool ReliableStreamTransport::ValidatePayloadStream(
        const ReliableStreamId streamId,
        const protocol::PacketType type,
        const std::uint8_t* payload,
        const std::size_t payloadSize) noexcept
    {
        if (!IsValidStream(streamId) || payload == nullptr || payloadSize == 0)
        {
            return false;
        }
        using protocol::PacketType;
        if (streamId.kind == ReliableStreamKind::Control)
        {
            return type == PacketType::Authority ||
                type == PacketType::PopulationState ||
                type == PacketType::SavedEntityMapBaseline;
        }
        if (streamId.kind == ReliableStreamKind::World)
        {
            if (type != PacketType::EntityLifecycle)
            {
                return false;
            }
            protocol::EntityLifecycleMessage message;
            return protocol::DecodeEntityLifecycleMessage(
                payload, payloadSize, message);
        }
        if (streamId.kind == ReliableStreamKind::Actor)
        {
            if (type == PacketType::PlayerAction)
            {
                protocol::PlayerActionMessage message;
                return protocol::DecodePlayerActionMessage(
                           payload, payloadSize, message) &&
                    message.ownerActorId == streamId.subject;
            }
            if (type == PacketType::PlayerActorState)
            {
                protocol::PlayerActorStateMessage message;
                return protocol::DecodePlayerActorStateMessage(
                           payload, payloadSize, message) &&
                    message.actorId == streamId.subject;
            }
            if (type == PacketType::EntityVitals)
            {
                protocol::EntityVitalsMessage message;
                return protocol::DecodeEntityVitalsMessage(
                           payload, payloadSize, message) &&
                    message.subject == protocol::EntityVitalsSubject::Player &&
                    message.playerActorId == streamId.subject;
            }
            if (type == PacketType::CombatHit)
            {
                protocol::CombatHitMessage message;
                return protocol::DecodeCombatHitMessage(
                           payload, payloadSize, message) &&
                    message.targetKind ==
                        protocol::CombatParticipantKind::Player &&
                    message.targetId == streamId.subject;
            }
            return false;
        }
        if (streamId.kind != ReliableStreamKind::Entity)
        {
            return false;
        }
        if (type == PacketType::EntityLifecycle)
        {
            protocol::EntityLifecycleMessage message;
            return protocol::DecodeEntityLifecycleMessage(
                       payload, payloadSize, message) &&
                message.entityUid == streamId.subject;
        }
        if (type == PacketType::EntityAction)
        {
            protocol::EntityActionMessage message;
            return protocol::DecodeEntityActionMessage(
                       payload, payloadSize, message) &&
                message.entityUid == streamId.subject;
        }
        if (type == PacketType::EntityLowSimulation)
        {
            protocol::EntityLowSimulationMessage message;
            return protocol::DecodeEntityLowSimulationMessage(
                       payload, payloadSize, message) &&
                message.entityUid == streamId.subject;
        }
        if (type == PacketType::EntityVitals)
        {
            protocol::EntityVitalsMessage message;
            return protocol::DecodeEntityVitalsMessage(
                       payload, payloadSize, message) &&
                message.subject == protocol::EntityVitalsSubject::WorldEntity &&
                message.entityUid == streamId.subject;
        }
        if (type == PacketType::CombatHit)
        {
            protocol::CombatHitMessage message;
            return protocol::DecodeCombatHitMessage(
                       payload, payloadSize, message) &&
                message.targetKind ==
                    protocol::CombatParticipantKind::WorldEntity &&
                message.targetId == streamId.subject;
        }
        return false;
    }

    bool ReliableStreamTransport::CanEnqueue(
        const ReliableStreamId streamId) const
    {
        if (Count(outbound_) >= TotalQueueLimit)
        {
            return false;
        }
        const auto stream = outbound_.find(streamId);
        return (stream != outbound_.end() ||
            outbound_.size() < StreamMetadataLimit) &&
            (stream == outbound_.end() ||
                stream->second.queue.size() < PerStreamQueueLimit);
    }

    bool ReliableStreamTransport::CanEnqueueMessage(
        const ReliableStreamId streamId,
        const protocol::PacketType type,
        const std::uint8_t* payload,
        const std::size_t payloadSize) const
    {
        return ValidatePayloadStream(streamId, type, payload, payloadSize) &&
            payloadSize <= protocol::MaximumPayloadBytes() &&
            CanEnqueue(streamId);
    }

    bool ReliableStreamTransport::IsMessageValid(
        const ReliableStreamId streamId,
        const protocol::PacketType type,
        const std::uint8_t* payload,
        const std::size_t payloadSize) noexcept
    {
        return ValidatePayloadStream(streamId, type, payload, payloadSize) &&
            payloadSize <= protocol::MaximumPayloadBytes();
    }

    bool ReliableStreamTransport::Enqueue(
        const ReliableStreamId streamId,
        const protocol::PacketType type,
        const std::uint64_t sourceActorId,
        const std::uint8_t* payload,
        const std::size_t payloadSize)
    {
        if (sourceActorId == 0 ||
            !CanEnqueueMessage(streamId, type, payload, payloadSize))
        {
            return false;
        }
        OutboundStream& stream = outbound_[streamId];
        if (stream.incarnation == 0)
        {
            stream.incarnation = AllocateIncarnation();
            if (stream.incarnation == 0)
            {
                outbound_.erase(streamId);
                return false;
            }
            stream.nextSequence = 1u;
        }
        TransportMessage message;
        message.type = type;
        message.sourceActorId = sourceActorId;
        message.streamId = streamId;
        message.streamIncarnation = stream.incarnation;
        message.sequence = stream.nextSequence;
        stream.nextSequence = NextSequence(stream.nextSequence);
        message.payloadSize = payloadSize;
        std::memcpy(message.payload.data(), payload, payloadSize);
        stream.queue.push_back(std::move(message));
        return true;
    }

    std::uint64_t ReliableStreamTransport::AllocateIncarnation() noexcept
    {
        if (nextIncarnation_ == 0)
        {
            nextIncarnation_ = ConnectionNonceRegistry::GenerateLocal();
        }
        if (nextIncarnation_ == 0 ||
            nextIncarnation_ == (std::numeric_limits<std::uint64_t>::max)())
        {
            return 0;
        }
        const std::uint64_t result = nextIncarnation_;
        ++nextIncarnation_;
        return result;
    }

    bool ReliableStreamTransport::ReclaimCompletedStream() noexcept
    {
        while (!completedStreams_.empty())
        {
            const CompletedStream completed = completedStreams_.front();
            completedStreams_.pop_front();
            const auto inbound = inbound_.find(completed.streamId);
            const auto incarnation = receivedIncarnations_.find(
                completed.streamId);
            if (inbound != inbound_.end() ||
                incarnation == receivedIncarnations_.end() ||
                incarnation->second != completed.incarnation ||
                completed.streamId.kind == ReliableStreamKind::Control ||
                completed.streamId.kind == ReliableStreamKind::World)
            {
                continue;
            }
            RememberRetiredStream(completed);
            receivedSequences_.erase(completed.streamId);
            receivedIncarnations_.erase(incarnation);
            return true;
        }
        return false;
    }

    void ReliableStreamTransport::RememberRetiredStream(
        const CompletedStream stream) noexcept
    {
        auto& retiredIncarnation = retiredIncarnations_[stream.streamId];
        if (stream.incarnation <= retiredIncarnation)
        {
            return;
        }
        retiredIncarnation = stream.incarnation;
        retiredStreams_.push_back(stream);
        while (retiredStreams_.size() > StreamMetadataLimit)
        {
            const CompletedStream expired = retiredStreams_.front();
            retiredStreams_.pop_front();
            const auto retained = retiredIncarnations_.find(expired.streamId);
            if (retained != retiredIncarnations_.end() &&
                retained->second == expired.incarnation)
            {
                retiredIncarnations_.erase(retained);
            }
        }
    }

    bool ReliableStreamTransport::AcceptAcknowledgement(
        const ReliableStreamId streamId,
        const std::uint64_t streamIncarnation,
        const std::uint32_t sequence)
    {
        const auto stream = outbound_.find(streamId);
        if (stream == outbound_.end() || streamIncarnation == 0 ||
            stream->second.incarnation != streamIncarnation ||
            stream->second.queue.empty() ||
            stream->second.queue.front().sequence != sequence)
        {
            return false;
        }
        stream->second.acknowledgedSequence = sequence;
        return true;
    }

    ReliableReceiveResult ReliableStreamTransport::AcceptIncoming(
        TransportMessage message)
    {
        if (message.sequence == 0 || message.streamIncarnation == 0 ||
            !ValidatePayloadStream(
                message.streamId,
                message.type,
                message.payload.data(),
                message.payloadSize))
        {
            return ReliableReceiveResult::Rejected;
        }
        bool knownStream =
            receivedSequences_.find(message.streamId) !=
            receivedSequences_.end();
        if (!knownStream && receivedSequences_.size() >= StreamMetadataLimit)
        {
            if (!ReclaimCompletedStream())
            {
                return ReliableReceiveResult::Backpressured;
            }
            knownStream = false;
        }
        auto incarnation = receivedIncarnations_.find(message.streamId);
        if (incarnation != receivedIncarnations_.end() &&
            incarnation->second != message.streamIncarnation)
        {
            const auto inbound = inbound_.find(message.streamId);
            if (message.sequence != 1 ||
                (inbound != inbound_.end() && !inbound->second.empty()) ||
                message.streamIncarnation <= incarnation->second)
            {
                return ReliableReceiveResult::Rejected;
            }
            incarnation->second = message.streamIncarnation;
            receivedSequences_[message.streamId] = 0;
        }
        if (incarnation == receivedIncarnations_.end())
        {
            const auto retired = retiredIncarnations_.find(message.streamId);
            if (retired != retiredIncarnations_.end() &&
                message.streamIncarnation <= retired->second)
            {
                return ReliableReceiveResult::Rejected;
            }
            receivedIncarnations_.emplace(
                message.streamId,
                message.streamIncarnation);
        }
        std::uint32_t& previous = receivedSequences_[message.streamId];
        if (message.sequence == previous)
        {
            return ReliableReceiveResult::Duplicate;
        }
        if (previous != 0 && message.sequence != NextSequence(previous))
        {
            return ReliableReceiveResult::Rejected;
        }
        std::deque<TransportMessage>& stream = inbound_[message.streamId];
        if (stream.size() >= PerStreamQueueLimit)
        {
            return ReliableReceiveResult::Backpressured;
        }
        previous = message.sequence;
        stream.push_back(std::move(message));
        if (readyStreamSet_.insert(stream.front().streamId).second)
        {
            readyStreams_.push_back(stream.front().streamId);
        }
        return ReliableReceiveResult::Accepted;
    }

    std::vector<TransportMessage> ReliableStreamTransport::Due(
        const std::uint64_t now,
        const std::uint64_t resendMilliseconds)
    {
        std::vector<TransportMessage> result;
        std::vector<ReliableStreamId> emptyStreams;
        for (auto& [streamId, stream] : outbound_)
        {
            while (!stream.queue.empty() &&
                stream.acknowledgedSequence == stream.queue.front().sequence)
            {
                stream.queue.pop_front();
                stream.lastSentSequence = 0;
                stream.lastSentAt = 0;
            }
            if (stream.queue.empty())
            {
                emptyStreams.push_back(streamId);
                continue;
            }
            const TransportMessage& message = stream.queue.front();
            if (stream.lastSentSequence != message.sequence ||
                now - stream.lastSentAt >= resendMilliseconds)
            {
                result.push_back(message);
                stream.lastSentSequence = message.sequence;
                stream.lastSentAt = now;
            }
        }
        for (const ReliableStreamId streamId : emptyStreams)
        {
            outbound_.erase(streamId);
        }
        return result;
    }

    bool ReliableStreamTransport::TryConsume(TransportMessage& message)
    {
        if (readyStreams_.empty())
        {
            return false;
        }
        const ReliableStreamId streamId = readyStreams_.front();
        readyStreams_.pop_front();
        auto stream = inbound_.find(streamId);
        if (stream == inbound_.end() || stream->second.empty())
        {
            readyStreamSet_.erase(streamId);
            return false;
        }
        message = std::move(stream->second.front());
        stream->second.pop_front();
        if (!stream->second.empty())
        {
            readyStreams_.push_back(streamId);
        }
        else
        {
            readyStreamSet_.erase(streamId);
            completedStreams_.push_back({
                streamId,
                message.streamIncarnation});
            while (completedStreams_.size() > StreamMetadataLimit)
            {
                completedStreams_.pop_front();
            }
            inbound_.erase(stream);
        }
        return true;
    }

    void ReliableStreamTransport::Clear() noexcept
    {
        outbound_.clear();
        receivedSequences_.clear();
        receivedIncarnations_.clear();
        inbound_.clear();
        readyStreams_.clear();
        readyStreamSet_.clear();
        completedStreams_.clear();
        retiredIncarnations_.clear();
        retiredStreams_.clear();
    }
}
