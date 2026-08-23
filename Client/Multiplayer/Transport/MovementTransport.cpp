#include "MovementTransport.h"

#include <cstring>

namespace fable::multiplayer
{
    std::size_t MovementTransport::MovementSubjectHash::operator()(
        const MovementSubject& subject) const noexcept
    {
        std::size_t hash = static_cast<std::size_t>(subject.sourceActorId);
        hash ^= static_cast<std::size_t>(subject.entityUid) +
            static_cast<std::size_t>(0x9E3779B97F4A7C15ull) +
            (hash << 6) + (hash >> 2);
        hash ^= static_cast<std::size_t>(subject.entityGeneration) +
            static_cast<std::size_t>(0x9E3779B9u) +
            (hash << 6) + (hash >> 2);
        return hash;
    }

    bool MovementTransport::DecodeSubject(
        const std::uint64_t sourceActorId,
        const std::uint8_t* payload,
        const std::size_t payloadSize,
        MovementSubject& subject,
        std::uint32_t& sequence) noexcept
    {
        protocol::EntityMovementMessage movement;
        if (sourceActorId == 0 || payload == nullptr ||
            !protocol::DecodeEntityMovementMessage(
                payload, payloadSize, movement))
        {
            return false;
        }
        subject = {
            sourceActorId,
            movement.entityUid,
            movement.entityGeneration};
        sequence = movement.sequence;
        return true;
    }

    bool MovementTransport::QueueEntity(
        const std::uint64_t sourceActorId,
        const protocol::PacketType type,
        const std::uint8_t* payload,
        const std::size_t payloadSize)
    {
        MovementSubject subject;
        std::uint32_t sequence = 0;
        if (type != protocol::PacketType::EntityMovement ||
            payload == nullptr || payloadSize == 0 ||
            payloadSize > protocol::MaximumPayloadBytes() ||
            !DecodeSubject(
                sourceActorId, payload, payloadSize, subject, sequence))
        {
            return false;
        }
        const auto existing = entityOutbound_.find(subject);
        if (existing != entityOutbound_.end() &&
            !IsNewerSequence(sequence, existing->second.sequence))
        {
            // Outbound movement is replace-in-place. A duplicate or delayed
            // producer sample is already represented by the newer slot.
            return true;
        }
        if (existing == entityOutbound_.end() &&
            entityOutbound_.size() >= UnreliableQueueLimit)
        {
            return false;
        }
        TransportMessage message;
        message.type = type;
        message.sourceActorId = sourceActorId;
        message.payloadSize = payloadSize;
        std::memcpy(message.payload.data(), payload, payloadSize);
        entityOutbound_.insert_or_assign(
            subject,
            EntityMovementSlot{std::move(message), sequence});
        return true;
    }

    std::vector<TransportMessage> MovementTransport::TakeEntityOutbound()
    {
        std::vector<TransportMessage> messages;
        messages.reserve(entityOutbound_.size());
        for (auto& [subject, slot] : entityOutbound_)
        {
            (void)subject;
            messages.push_back(std::move(slot.message));
        }
        entityOutbound_.clear();
        return messages;
    }

    bool MovementTransport::AcceptEntity(TransportMessage message)
    {
        if (message.type != protocol::PacketType::EntityMovement ||
            message.sourceActorId == 0 || message.payloadSize == 0 ||
            message.payloadSize > message.payload.size())
        {
            return false;
        }
        MovementSubject subject;
        std::uint32_t sequence = 0;
        if (!DecodeSubject(
                message.sourceActorId,
                message.payload.data(),
                message.payloadSize,
                subject,
                sequence))
        {
            return false;
        }
        const auto existing = entityInbound_.find(subject);
        if (existing != entityInbound_.end() &&
            !IsNewerSequence(sequence, existing->second.sequence))
        {
            return false;
        }
        if (existing == entityInbound_.end() &&
            entityInbound_.size() >= UnreliableQueueLimit)
        {
            return false;
        }
        entityInbound_.insert_or_assign(
            subject,
            EntityMovementSlot{std::move(message), sequence});
        if (readyEntitySet_.insert(subject).second)
        {
            readyEntities_.push_back(subject);
        }
        return true;
    }

    bool MovementTransport::TryConsumeEntity(TransportMessage& message)
    {
        if (readyEntities_.empty())
        {
            return false;
        }
        const MovementSubject subject = readyEntities_.front();
        readyEntities_.pop_front();
        const auto iterator = entityInbound_.find(subject);
        if (iterator == entityInbound_.end())
        {
            readyEntitySet_.erase(subject);
            return false;
        }
        message = std::move(iterator->second.message);
        entityInbound_.erase(iterator);
        readyEntitySet_.erase(subject);
        return true;
    }

    bool MovementTransport::IsNewerSequence(
        const std::uint32_t candidate,
        const std::uint32_t previous) noexcept
    {
        return previous == 0 ||
            static_cast<std::int32_t>(candidate - previous) > 0;
    }

    void MovementTransport::QueuePlayer(const PlayerState& state)
    {
        for (auto iterator = playerInbound_.begin();
             iterator != playerInbound_.end();)
        {
            if (iterator->actorId != state.actorId)
            {
                ++iterator;
                continue;
            }
            if (iterator->authorityEpoch == state.authorityEpoch &&
                iterator->actorGeneration == state.actorGeneration &&
                iterator->mapEpoch == state.mapEpoch)
            {
                const std::uint32_t changed =
                    iterator->changedProperties | state.changedProperties;
                *iterator = state;
                iterator->changedProperties = changed;
                return;
            }
            iterator = playerInbound_.erase(iterator);
        }
        if (playerInbound_.size() >= PlayerInboxLimit)
        {
            playerInbound_.pop_front();
        }
        playerInbound_.push_back(state);
    }

    bool MovementTransport::AcceptPlayer(const PlayerState& state)
    {
        if (state.actorId == 0 || state.changedProperties == 0)
        {
            return false;
        }
        MovementFence& previous = playerFences_[state.actorId];
        if (previous.authorityEpoch != state.authorityEpoch ||
            previous.actorGeneration != state.actorGeneration ||
            previous.mapEpoch != state.mapEpoch)
        {
            previous = {
                state.authorityEpoch,
                state.actorGeneration,
                state.mapEpoch,
                0};
        }
        if (!IsNewerSequence(state.sequence, previous.sequence))
        {
            return false;
        }
        previous.sequence = state.sequence;
        QueuePlayer(state);
        return true;
    }

    bool MovementTransport::TryConsumePlayer(PlayerState& state)
    {
        if (playerInbound_.empty())
        {
            return false;
        }
        state = std::move(playerInbound_.front());
        playerInbound_.pop_front();
        return true;
    }

    void MovementTransport::ForgetActor(const std::uint64_t actorId)
    {
        playerFences_.erase(actorId);
        for (auto iterator = playerInbound_.begin();
             iterator != playerInbound_.end();)
        {
            if (iterator->actorId == actorId)
            {
                iterator = playerInbound_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }

    void MovementTransport::Clear() noexcept
    {
        entityOutbound_.clear();
        entityInbound_.clear();
        readyEntities_.clear();
        readyEntitySet_.clear();
        playerFences_.clear();
        playerInbound_.clear();
    }
}
