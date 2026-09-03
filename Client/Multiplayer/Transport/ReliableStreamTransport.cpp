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
#include <algorithm>
#include <chrono>
#include <type_traits>

namespace
{
    constexpr std::uint32_t kReliableFragmentMagic = 0x52474654u;
    constexpr std::uint8_t kReliableFragmentVersion = 1;

#pragma pack(push, 1)
    struct ReliableFragmentWire final
    {
        std::uint32_t magic = kReliableFragmentMagic;
        std::uint8_t version = kReliableFragmentVersion;
        std::uint8_t originalType = 0;
        std::uint16_t fragmentIndex = 0;
        std::uint16_t fragmentCount = 0;
        std::uint16_t chunkSize = 0;
        std::uint32_t totalSize = 0;
        std::uint32_t logicalSequence = 0;
        std::uint32_t reserved = 0;
    };
#pragma pack(pop)

    static_assert(sizeof(ReliableFragmentWire) == 24);
    static_assert(std::is_trivially_copyable_v<ReliableFragmentWire>);

    struct ParsedFragment final
    {
        ReliableFragmentWire wire = {};
        const std::uint8_t* chunk = nullptr;
    };

    bool ParseFragment(
        const fable::multiplayer::TransportMessage& message,
        ParsedFragment& output) noexcept
    {
        using namespace fable::multiplayer;
        using protocol::PacketType;
        output = {};
        if (message.type != PacketType::ReliableFragment ||
            message.streamId.kind != ReliableStreamKind::Actor ||
            message.streamId.subject == 0 ||
            message.payloadSize < sizeof(ReliableFragmentWire) ||
            message.payloadSize > protocol::MaximumPayloadBytes())
        {
            return false;
        }
        std::memcpy(&output.wire, message.payload.data(), sizeof(output.wire));
        if (output.wire.magic != kReliableFragmentMagic ||
            output.wire.version != kReliableFragmentVersion ||
            output.wire.originalType != static_cast<std::uint8_t>(
                PacketType::PlayerActorState) ||
            output.wire.fragmentCount < 2 ||
            output.wire.fragmentCount > ReliableStreamTransport::MaximumFragmentCount ||
            output.wire.fragmentIndex >= output.wire.fragmentCount ||
            output.wire.chunkSize != message.payloadSize - sizeof(output.wire) ||
            output.wire.chunkSize == 0 ||
            output.wire.totalSize == 0 ||
            output.wire.totalSize > protocol::MaximumReliableMessageBytes ||
            output.wire.logicalSequence == 0 || output.wire.reserved != 0 ||
            output.wire.chunkSize > ReliableStreamTransport::MaximumFragmentPayloadBytes)
        {
            return false;
        }
        const std::size_t offset = static_cast<std::size_t>(
            output.wire.fragmentIndex) *
            ReliableStreamTransport::MaximumFragmentPayloadBytes;
        if (offset >= output.wire.totalSize ||
            offset + output.wire.chunkSize > output.wire.totalSize)
        {
            return false;
        }
        const std::size_t expectedCount =
            (output.wire.totalSize +
                ReliableStreamTransport::MaximumFragmentPayloadBytes - 1) /
            ReliableStreamTransport::MaximumFragmentPayloadBytes;
        if (expectedCount != output.wire.fragmentCount ||
            (output.wire.fragmentIndex + 1 < output.wire.fragmentCount &&
                output.wire.chunkSize !=
                    ReliableStreamTransport::MaximumFragmentPayloadBytes) ||
            (output.wire.fragmentIndex + 1 == output.wire.fragmentCount &&
                output.wire.chunkSize != output.wire.totalSize - offset))
        {
            return false;
        }
        output.chunk = message.payload.data() + sizeof(output.wire);
        return true;
    }

    std::uint64_t NowMilliseconds() noexcept
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }
}

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
            count += stream.logicalQueueSize;
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
                type == PacketType::SavedEntityMapBaseline ||
                type == PacketType::QuestStateSnapshot ||
                type == PacketType::WorldSectionSnapshot;
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
                stream->second.logicalQueueSize < PerStreamQueueLimit);
    }

    bool ReliableStreamTransport::CanEnqueueMessage(
        const ReliableStreamId streamId,
        const protocol::PacketType type,
        const std::uint8_t* payload,
        const std::size_t payloadSize) const
    {
        if (!ValidatePayloadStream(streamId, type, payload, payloadSize) ||
            payloadSize > protocol::MaximumReliableMessageBytes)
        {
            return false;
        }
        const std::size_t fragmentCount =
            payloadSize <= protocol::MaximumPayloadBytes()
                ? 1
                : (payloadSize + MaximumFragmentPayloadBytes - 1) /
                    MaximumFragmentPayloadBytes;
        if (fragmentCount > MaximumFragmentCount ||
            Count(outbound_) + 1 > TotalQueueLimit)
        {
            return false;
        }
        const auto stream = outbound_.find(streamId);
        return (stream != outbound_.end() ||
            outbound_.size() < StreamMetadataLimit) &&
            (stream == outbound_.end() ||
            stream->second.logicalQueueSize + 1 <=
                PerStreamQueueLimit);
    }

    bool ReliableStreamTransport::IsMessageValid(
        const ReliableStreamId streamId,
        const protocol::PacketType type,
        const std::uint8_t* payload,
        const std::size_t payloadSize) noexcept
    {
        if (!ValidatePayloadStream(streamId, type, payload, payloadSize) ||
            payloadSize > protocol::MaximumReliableMessageBytes)
        {
            return false;
        }
        return payloadSize <= protocol::MaximumPayloadBytes() ||
            (type == protocol::PacketType::PlayerActorState &&
                streamId.kind == ReliableStreamKind::Actor &&
                (payloadSize + MaximumFragmentPayloadBytes - 1) /
                    MaximumFragmentPayloadBytes <= MaximumFragmentCount);
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
            stream.nextLogicalSequence = 1u;
        }
        if (payloadSize <= protocol::MaximumPayloadBytes())
        {
            TransportMessage message;
            message.type = type;
            message.sourceActorId = sourceActorId;
            message.streamId = streamId;
            message.streamIncarnation = stream.incarnation;
            message.sequence = stream.nextSequence;
            stream.nextSequence = NextSequence(stream.nextSequence);
            stream.nextLogicalSequence = NextSequence(stream.nextLogicalSequence);
            message.payloadSize = payloadSize;
            std::memcpy(message.payload.data(), payload, payloadSize);
            stream.queue.push_back(std::move(message));
            ++stream.logicalQueueSize;
            return true;
        }

        const std::size_t fragmentCount =
            (payloadSize + MaximumFragmentPayloadBytes - 1) /
            MaximumFragmentPayloadBytes;
        if (fragmentCount > MaximumFragmentCount)
        {
            return false;
        }
        const std::uint32_t logicalSequence = stream.nextLogicalSequence;
        for (std::size_t index = 0; index < fragmentCount; ++index)
        {
            const std::size_t offset = index * MaximumFragmentPayloadBytes;
            const std::size_t chunkSize = std::min(
                MaximumFragmentPayloadBytes, payloadSize - offset);
            ReliableFragmentWire header;
            header.originalType = static_cast<std::uint8_t>(type);
            header.fragmentIndex = static_cast<std::uint16_t>(index);
            header.fragmentCount = static_cast<std::uint16_t>(fragmentCount);
            header.chunkSize = static_cast<std::uint16_t>(chunkSize);
            header.totalSize = static_cast<std::uint32_t>(payloadSize);
            header.logicalSequence = logicalSequence;

            TransportMessage message;
            message.type = protocol::PacketType::ReliableFragment;
            message.sourceActorId = sourceActorId;
            message.streamId = streamId;
            message.streamIncarnation = stream.incarnation;
            message.sequence = stream.nextSequence;
            stream.nextSequence = NextSequence(stream.nextSequence);
            message.payloadSize = sizeof(header) + chunkSize;
            message.logicalMessageEnd = index + 1 == fragmentCount;
            std::memcpy(message.payload.data(), &header, sizeof(header));
            std::memcpy(
                message.payload.data() + sizeof(header), payload + offset,
                chunkSize);
            stream.queue.push_back(std::move(message));
        }
        stream.nextLogicalSequence = NextSequence(stream.nextLogicalSequence);
        ++stream.logicalQueueSize;
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

    void ReliableStreamTransport::ExpireReassemblies(
        const std::uint64_t now) noexcept
    {
        for (auto iterator = reassemblies_.begin();
             iterator != reassemblies_.end();)
        {
            const Reassembly& reassembly = iterator->second;
            if (now < reassembly.lastTouchedAt ||
                now - reassembly.lastTouchedAt <=
                    ReassemblyTimeoutMilliseconds)
            {
                ++iterator;
                continue;
            }

            const auto incarnation = receivedIncarnations_.find(
                iterator->first.streamId);
            const auto sequence = receivedSequences_.find(
                iterator->first.streamId);
            if (incarnation != receivedIncarnations_.end() &&
                sequence != receivedSequences_.end() &&
                incarnation->second == iterator->first.incarnation &&
                sequence->second == reassembly.firstSequence)
            {
                sequence->second = reassembly.firstSequence == 1
                    ? 0
                    : reassembly.firstSequence - 1;
            }
            iterator = reassemblies_.erase(iterator);
        }
    }

    bool ReliableStreamTransport::AcceptFragment(TransportMessage message)
    {
        ParsedFragment fragment;
        if (!ParseFragment(message, fragment))
        {
            return false;
        }
        const std::uint64_t now = NowMilliseconds();
        const ReassemblyKey key = {
            message.streamId,
            message.streamIncarnation,
            fragment.wire.logicalSequence};
        auto iterator = reassemblies_.find(key);
        if (iterator == reassemblies_.end())
        {
            if (fragment.wire.fragmentIndex != 0)
            {
                return false;
            }
            if (reassemblies_.size() >= 32)
            {
                return false;
            }
            Reassembly reassembly;
            reassembly.originalType = static_cast<protocol::PacketType>(
                fragment.wire.originalType);
            reassembly.sourceActorId = message.sourceActorId;
            reassembly.connectionNonce = message.connectionNonce;
            reassembly.fragmentCount = fragment.wire.fragmentCount;
            reassembly.totalSize = fragment.wire.totalSize;
            reassembly.lastTouchedAt = now;
            reassembly.firstSequence = message.sequence;
            iterator = reassemblies_.emplace(key, std::move(reassembly)).first;
        }
        Reassembly& reassembly = iterator->second;
        if (reassembly.sourceActorId != message.sourceActorId ||
            reassembly.connectionNonce != message.connectionNonce ||
            reassembly.fragmentCount != fragment.wire.fragmentCount ||
            reassembly.totalSize != fragment.wire.totalSize ||
            reassembly.receivedMask &
                static_cast<std::uint16_t>(1u << fragment.wire.fragmentIndex))
        {
            return false;
        }
        reassembly.lastTouchedAt = now;
        const std::size_t offset = static_cast<std::size_t>(
            fragment.wire.fragmentIndex) * MaximumFragmentPayloadBytes;
        std::memcpy(
            reassembly.payload.data() + offset,
            fragment.chunk,
            fragment.wire.chunkSize);
        reassembly.receivedMask |= static_cast<std::uint16_t>(
            1u << fragment.wire.fragmentIndex);
        ++reassembly.receivedCount;
        if (reassembly.receivedCount != reassembly.fragmentCount)
        {
            return true;
        }

        TransportMessage complete;
        complete.type = reassembly.originalType;
        complete.sourceActorId = reassembly.sourceActorId;
        complete.connectionNonce = reassembly.connectionNonce;
        complete.streamId = message.streamId;
        complete.streamIncarnation = message.streamIncarnation;
        complete.sequence = fragment.wire.logicalSequence;
        complete.payloadSize = reassembly.totalSize;
        std::memcpy(
            complete.payload.data(), reassembly.payload.data(),
            reassembly.totalSize);
        reassemblies_.erase(iterator);

        std::deque<TransportMessage>& stream = inbound_[complete.streamId];
        stream.push_back(std::move(complete));
        if (readyStreamSet_.insert(stream.front().streamId).second)
        {
            readyStreams_.push_back(stream.front().streamId);
        }
        return true;
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
            stream->second.queue.empty() || sequence == 0)
        {
            return false;
        }
        // ACK datagrams themselves may arrive out of order. Retain each ACK
        // until the contiguous queue prefix can be reclaimed; this preserves
        // progress when a later ACK overtakes an earlier one on the wire.
        for (const TransportMessage& message : stream->second.queue)
        {
            if (message.sequence == sequence)
            {
                if (stream->second.sentAt.find(sequence) ==
                    stream->second.sentAt.end())
                {
                    // A queued packet must have been emitted before its ACK
                    // can retire it. This also prevents a forged ACK from
                    // skipping the unsent tail outside the send window.
                    return false;
                }
                if (message.type == protocol::PacketType::ReliableFragment &&
                    message.logicalMessageEnd)
                {
                    // UdpPeer acknowledges only the final fragment. Treat
                    // that ACK as cumulative for this logical message so a
                    // timed-out partial reassembly can be retransmitted from
                    // its first fragment.
                    for (const TransportMessage& preceding :
                         stream->second.queue)
                    {
                        stream->second.acknowledgedSequences.insert(
                            preceding.sequence);
                        if (preceding.sequence == sequence)
                        {
                            break;
                        }
                    }
                    return true;
                }
                stream->second.acknowledgedSequences.insert(sequence);
                return true;
            }
        }
        return false;
    }

    ReliableReceiveResult ReliableStreamTransport::AcceptIncoming(
        TransportMessage message)
    {
        acknowledgeLastIncoming_ = false;
        ExpireReassemblies(NowMilliseconds());
        ParsedFragment fragment;
        const bool isFragment =
            message.type == protocol::PacketType::ReliableFragment;
        if (message.sequence == 0 || message.streamIncarnation == 0 ||
            (isFragment
                ? !ParseFragment(message, fragment)
                : !ValidatePayloadStream(
                    message.streamId,
                    message.type,
                    message.payload.data(),
                    message.payloadSize)))
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
        // A newly observed stream must begin at sequence one. Without this
        // fence, a reordered second packet could become the stream origin and
        // permanently skip the construction/state record ahead of it.
        if (previous == 0 && message.sequence != 1)
        {
            return ReliableReceiveResult::Rejected;
        }
        if (message.sequence == previous)
        {
            acknowledgeLastIncoming_ = !isFragment ||
                fragment.wire.fragmentIndex + 1 ==
                    fragment.wire.fragmentCount;
            return ReliableReceiveResult::Duplicate;
        }
        if (previous != 0 && message.sequence != NextSequence(previous))
        {
            return ReliableReceiveResult::Rejected;
        }
        if (isFragment)
        {
            const ReassemblyKey key = {
                message.streamId,
                message.streamIncarnation,
                fragment.wire.logicalSequence};
            const auto reassembly = reassemblies_.find(key);
            if (reassembly == reassemblies_.end() &&
                fragment.wire.fragmentIndex != 0)
            {
                return ReliableReceiveResult::Rejected;
            }
            if (reassembly == reassemblies_.end() &&
                reassemblies_.size() >= 32)
            {
                return ReliableReceiveResult::Backpressured;
            }
            if (fragment.wire.fragmentIndex + 1 ==
                    fragment.wire.fragmentCount)
            {
                const auto inbound = inbound_.find(message.streamId);
                if (inbound != inbound_.end() &&
                    inbound->second.size() >= PerStreamQueueLimit)
                {
                    return ReliableReceiveResult::Backpressured;
                }
            }
        }
        if (isFragment)
        {
            const std::uint32_t acceptedSequence = message.sequence;
            if (!AcceptFragment(std::move(message)))
            {
                return ReliableReceiveResult::Rejected;
            }
            // Commit sequence advancement only after fragment validation and
            // reassembly succeeded; rejected fragments must not poison the
            // ordered stream's next expected sequence.
            previous = acceptedSequence;
            acknowledgeLastIncoming_ = fragment.wire.fragmentIndex + 1 ==
                fragment.wire.fragmentCount;
            return ReliableReceiveResult::Accepted;
        }
        std::deque<TransportMessage>& stream = inbound_[message.streamId];
        if (stream.size() >= PerStreamQueueLimit)
        {
            return ReliableReceiveResult::Backpressured;
        }
        previous = message.sequence;
        stream.push_back(std::move(message));
        acknowledgeLastIncoming_ = true;
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
                stream.acknowledgedSequences.erase(
                    stream.queue.front().sequence) != 0)
            {
                stream.sentAt.erase(stream.queue.front().sequence);
                if (stream.queue.front().logicalMessageEnd &&
                    stream.logicalQueueSize != 0)
                {
                    --stream.logicalQueueSize;
                }
                stream.queue.pop_front();
            }
            if (stream.queue.empty())
            {
                emptyStreams.push_back(streamId);
                continue;
            }
            // Fragment and complete records share the same bounded window.
            // The receiver still rejects gaps and only advances its
            // contiguous cursor, so loss or reordering merely schedules the
            // missing record for retransmission without changing delivery
            // order.
            constexpr std::size_t windowLimit = ReliableWindowSize;
            std::size_t inFlight = 0;
            for (const TransportMessage& message : stream.queue)
            {
                if (stream.acknowledgedSequences.find(message.sequence) !=
                    stream.acknowledgedSequences.end())
                {
                    continue;
                }
                if (inFlight >= windowLimit)
                {
                    break;
                }
                const auto sent = stream.sentAt.find(message.sequence);
                if (sent == stream.sentAt.end())
                {
                    result.push_back(message);
                    stream.sentAt.emplace(message.sequence, now);
                }
                else if (now >= sent->second &&
                    now - sent->second >= resendMilliseconds)
                {
                    result.push_back(message);
                    sent->second = now;
                }
                ++inFlight;
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
        reassemblies_.clear();
        acknowledgeLastIncoming_ = false;
    }
}
