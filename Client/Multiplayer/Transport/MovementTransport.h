#pragma once

#include "Multiplayer/Protocol/EntityMovementMessage.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "TransportMessage.h"

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fable::multiplayer
{
    // Bounded, lossy movement transport. It coalesces entity samples, fences
    // actor incarnations, and presents fair FIFO consumption to the session.
    class MovementTransport final
    {
    public:
        static constexpr std::size_t UnreliableQueueLimit = 4096;
        static constexpr std::size_t PlayerInboxLimit = 256;

        bool QueueEntity(
            std::uint64_t sourceActorId,
            protocol::PacketType type,
            const std::uint8_t* payload,
            std::size_t payloadSize);
        [[nodiscard]] std::vector<TransportMessage> TakeEntityOutbound();
        bool AcceptEntity(TransportMessage message);
        [[nodiscard]] bool TryConsumeEntity(TransportMessage& message);

        // Returns false for a stale or duplicate actor sample.
        bool AcceptPlayer(const PlayerState& state);
        [[nodiscard]] bool TryConsumePlayer(PlayerState& state);
        void ForgetActor(std::uint64_t actorId);
        void Clear() noexcept;

    private:
        struct MovementSubject final
        {
            std::uint64_t sourceActorId = 0;
            std::uint64_t entityUid = 0;
            std::uint32_t entityGeneration = 0;

            bool operator==(const MovementSubject& other) const noexcept
            {
                return sourceActorId == other.sourceActorId &&
                    entityUid == other.entityUid &&
                    entityGeneration == other.entityGeneration;
            }
        };

        struct MovementSubjectHash final
        {
            std::size_t operator()(
                const MovementSubject& subject) const noexcept;
        };

        struct MovementFence final
        {
            std::uint32_t authorityEpoch = 0;
            std::uint32_t actorGeneration = 0;
            std::uint32_t mapEpoch = 0;
            std::uint32_t sequence = 0;
        };

        struct EntityMovementSlot final
        {
            TransportMessage message;
            std::uint32_t sequence = 0;
        };

        static bool DecodeSubject(
            std::uint64_t sourceActorId,
            const std::uint8_t* payload,
            std::size_t payloadSize,
            MovementSubject& subject,
            std::uint32_t& sequence) noexcept;
        static bool IsNewerSequence(
            std::uint32_t candidate,
            std::uint32_t previous) noexcept;
        void QueuePlayer(const PlayerState& state);

        std::unordered_map<MovementSubject, EntityMovementSlot,
            MovementSubjectHash>
            entityOutbound_;
        std::unordered_map<MovementSubject, EntityMovementSlot,
            MovementSubjectHash>
            entityInbound_;
        std::deque<MovementSubject> readyEntities_;
        std::unordered_set<MovementSubject, MovementSubjectHash>
            readyEntitySet_;
        std::unordered_map<std::uint64_t, MovementFence> playerFences_;
        std::deque<PlayerState> playerInbound_;
    };
}
