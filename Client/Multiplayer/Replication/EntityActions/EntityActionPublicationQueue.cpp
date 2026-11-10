#include "EntityActionPublicationQueue.h"

#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <array>
#include <utility>

namespace fable::multiplayer::replication::entity_actions
{
    void EntityActionPublicationQueue::Initialize(
        UdpPeer& transport,
        const core::Diagnostics& diagnostics) noexcept
    {
        Clear();
        transport_ = &transport;
        diagnostics_ = diagnostics;
    }

    bool EntityActionPublicationQueue::Enqueue(
        protocol::EntityActionMessage message)
    {
        if (messages_.size() >= Capacity)
        {
            diagnostics_.Event(
                "MultiplayerEntityActionOverflow",
                "bounded semantic action publication queue is full");
            return false;
        }
        messages_.push_back(std::move(message));
        return true;
    }

    bool EntityActionPublicationQueue::Publish()
    {
        if (transport_ == nullptr)
        {
            return false;
        }
        while (!messages_.empty())
        {
            std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload = {};
            std::size_t payloadSize = 0;
            if (!protocol::EncodeEntityActionMessage(
                    messages_.front(),
                    payload.data(),
                    protocol::MaximumPayloadBytes(),
                    payloadSize))
            {
                return false;
            }
            if (!transport_->SubmitReliable(
                    reliable_stream::Entity(messages_.front().entityUid),
                    protocol::PacketType::EntityAction,
                    payload.data(),
                    payloadSize))
            {
                if (transport_->HasFailed())
                {
                    return false;
                }
                if (!backpressured_)
                {
                    diagnostics_.Event(
                        "MultiplayerEntityActionPublishDeferred",
                        "ordered baseline traffic is draining before queued action state");
                    backpressured_ = true;
                }
                return true;
            }
            messages_.pop_front();
        }
        if (backpressured_)
        {
            diagnostics_.Event(
                "MultiplayerEntityActionPublishResumed",
                "queued action state has entered the ordered transport");
            backpressured_ = false;
        }
        return true;
    }

    bool EntityActionPublicationQueue::Empty() const noexcept
    {
        return messages_.empty();
    }

    void EntityActionPublicationQueue::Clear() noexcept
    {
        messages_.clear();
        transport_ = nullptr;
        diagnostics_ = {};
        backpressured_ = false;
    }
}
