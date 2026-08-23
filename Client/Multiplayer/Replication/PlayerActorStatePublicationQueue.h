#pragma once

#include "Multiplayer/Protocol/PlayerActorStateMessage.h"

#include <cstddef>
#include <cstdint>
#include <deque>

namespace fable::core { struct Diagnostics; }
namespace fable::multiplayer { class UdpPeer; }

namespace fable::multiplayer::replication
{
    // Bounded ordered publication for structural player-actor state. Delta
    // messages for one incarnation coalesce; Construct/Retire/MapTransition
    // retain their reliable ordering. Transform snapshots never enter this
    // queue.
    class PlayerActorStatePublicationQueue final
    {
    public:
        static constexpr std::size_t Capacity = 1024;
        static constexpr std::size_t PerActorCapacity = 64;
        using DeltaMerger = protocol::PlayerActorStateMessage (*) (
            const protocol::PlayerActorStateMessage& current,
            const protocol::PlayerActorStateMessage& delta);

        void Initialize(const core::Diagnostics& diagnostics) noexcept;
        bool Enqueue(
            protocol::PlayerActorStateMessage message,
            DeltaMerger mergeDelta);
        bool Append(protocol::PlayerActorStateMessage message);
        [[nodiscard]] bool HasConstruct(std::uint64_t actorId) const noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;
        bool PublishPending(UdpPeer& transport);
        void Clear() noexcept;

    private:
        const core::Diagnostics* diagnostics_ = nullptr;
        std::deque<protocol::PlayerActorStateMessage> pending_;
        bool publishBackpressured_ = false;
    };
}
