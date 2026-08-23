#include "PlayerActorStatePublicationQueue.h"

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/PlayerActorStateCodec.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <array>
#include <unordered_set>
#include <utility>

namespace fable::multiplayer::replication
{
    void PlayerActorStatePublicationQueue::Initialize(
        const core::Diagnostics& diagnostics) noexcept
    {
        diagnostics_ = &diagnostics;
    }

    bool PlayerActorStatePublicationQueue::Enqueue(
        protocol::PlayerActorStateMessage message,
        const DeltaMerger mergeDelta)
    {
        std::size_t actorPendingCount = 0;
        auto mergeCandidate = pending_.end();
        for (auto iterator = pending_.begin(); iterator != pending_.end();
             ++iterator)
        {
            if (iterator->actorId != message.actorId)
            {
                continue;
            }
            ++actorPendingCount;
            if (message.operation == protocol::PlayerActorStateOperation::
                    ComponentDelta &&
                iterator->operation ==
                    protocol::PlayerActorStateOperation::ComponentDelta &&
                iterator->authorityEpoch == message.authorityEpoch &&
                iterator->actorGeneration == message.actorGeneration &&
                iterator->mapEpoch == message.mapEpoch)
            {
                mergeCandidate = iterator;
            }
        }
        if (mergeCandidate != pending_.end())
        {
            protocol::PlayerActorStateMessage merged =
                mergeDelta(*mergeCandidate, message);
            merged.structuralRevision = message.structuralRevision;
            *mergeCandidate = std::move(merged);
            return true;
        }
        if (actorPendingCount >= PerActorCapacity)
        {
            if (diagnostics_ != nullptr)
            {
                diagnostics_->Event(
                    "MultiplayerPlayerActorStateOverflow",
                    "one actor reached its bounded lifecycle publication quota");
            }
            return false;
        }
        if (pending_.size() >= Capacity)
        {
            if (diagnostics_ != nullptr)
            {
                diagnostics_->Event(
                    "MultiplayerPlayerActorStateOverflow",
                    "bounded actor lifecycle publication queue is full");
            }
            return false;
        }
        pending_.push_back(std::move(message));
        return true;
    }

    bool PlayerActorStatePublicationQueue::Append(
        protocol::PlayerActorStateMessage message)
    {
        if (pending_.size() >= Capacity)
        {
            return false;
        }
        pending_.push_back(std::move(message));
        return true;
    }

    bool PlayerActorStatePublicationQueue::HasConstruct(
        const std::uint64_t actorId) const noexcept
    {
        for (const auto& message : pending_)
        {
            if (message.actorId == actorId &&
                message.operation == protocol::PlayerActorStateOperation::
                    Construct)
            {
                return true;
            }
        }
        return false;
    }

    std::size_t PlayerActorStatePublicationQueue::Size() const noexcept
    {
        return pending_.size();
    }

    bool PlayerActorStatePublicationQueue::PublishPending(UdpPeer& transport)
    {
        const std::size_t scheduled = pending_.size();
        std::unordered_set<std::uint64_t> attemptedActors;
        bool deferred = false;
        for (std::size_t attempt = 0;
             attempt < scheduled && !pending_.empty(); ++attempt)
        {
            const std::uint64_t actorId = pending_.front().actorId;
            if (!attemptedActors.insert(actorId).second)
            {
                pending_.push_back(std::move(pending_.front()));
                pending_.pop_front();
                continue;
            }
            std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload = {};
            std::size_t payloadSize = 0;
            if (!protocol::EncodePlayerActorStateMessage(
                    pending_.front(), payload.data(),
                    protocol::MaximumPayloadBytes(), payloadSize))
            {
                if (diagnostics_ != nullptr)
                {
                    diagnostics_->Event(
                        "MultiplayerPlayerActorStateRejected",
                        "actor lifecycle payload failed bounded encoding");
                }
                return false;
            }
            if (!transport.SubmitReliable(
                    reliable_stream::Actor(actorId),
                    protocol::PacketType::PlayerActorState,
                    payload.data(), payloadSize))
            {
                if (transport.HasFailed())
                {
                    return false;
                }
                deferred = true;
                pending_.push_back(std::move(pending_.front()));
                pending_.pop_front();
                continue;
            }
            pending_.pop_front();
        }
        if (deferred && !publishBackpressured_)
        {
            publishBackpressured_ = true;
            if (diagnostics_ != nullptr)
            {
                diagnostics_->Event(
                    "MultiplayerPlayerActorStatePublishDeferred",
                    "one or more actor lifecycle streams are waiting for transport capacity");
            }
        }
        else if (!deferred && publishBackpressured_)
        {
            publishBackpressured_ = false;
            if (diagnostics_ != nullptr)
            {
                diagnostics_->Event(
                    "MultiplayerPlayerActorStatePublishResumed",
                    "queued actor lifecycle traffic entered ordered transport");
            }
        }
        return true;
    }

    void PlayerActorStatePublicationQueue::Clear() noexcept
    {
        pending_.clear();
        publishBackpressured_ = false;
    }
}
