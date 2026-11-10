#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/EntityActionMessage.h"

#include <cstddef>
#include <deque>

namespace fable::multiplayer
{
    class UdpPeer;
}

namespace fable::multiplayer::replication::entity_actions
{
    // Bounded ordered-publication lane for semantic entity actions. Authority
    // decides what may be queued; this class only owns encoding, reliable
    // stream backpressure, and queue lifetime.
    class EntityActionPublicationQueue final
    {
    public:
        void Initialize(
            UdpPeer& transport,
            const core::Diagnostics& diagnostics) noexcept;
        [[nodiscard]] bool Enqueue(
            protocol::EntityActionMessage message);
        [[nodiscard]] bool Publish();
        [[nodiscard]] bool Empty() const noexcept;
        void Clear() noexcept;

    private:
        static constexpr std::size_t Capacity = 8192;

        UdpPeer* transport_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::deque<protocol::EntityActionMessage> messages_;
        bool backpressured_ = false;
    };
}
