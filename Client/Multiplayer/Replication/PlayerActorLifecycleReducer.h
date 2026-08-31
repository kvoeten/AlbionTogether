#pragma once

#include "Multiplayer/Protocol/PlayerActorStateMessage.h"

#include <cstdint>

namespace fable::multiplayer::replication
{
    enum class PlayerActorLifecycleReduction : std::uint8_t
    {
        Rejected,
        Ignored,
        Applied,
    };

    // Pure operation reducer for the reliable player-actor lifecycle. Transport
    // ownership and connection-nonce checks remain with the callers; this type
    // owns only structural validity, ordering, and canonical state merging.
    class PlayerActorLifecycleReducer final
    {
    public:
        [[nodiscard]] static PlayerActorLifecycleReduction Reduce(
            const protocol::PlayerActorStateMessage* current,
            const protocol::PlayerActorStateMessage& incoming,
            protocol::PlayerActorStateMessage& next);

        [[nodiscard]] static bool IsNewer(
            std::uint32_t candidate,
            std::uint32_t current) noexcept;

        [[nodiscard]] static bool IsOlderIncarnation(
            const protocol::PlayerActorStateMessage& current,
            const protocol::PlayerActorStateMessage& incoming) noexcept;

        [[nodiscard]] static protocol::PlayerActorStateMessage MergeDelta(
            const protocol::PlayerActorStateMessage& current,
            const protocol::PlayerActorStateMessage& delta);

        [[nodiscard]] static protocol::PlayerActorStateMessage CoalesceDelta(
            const protocol::PlayerActorStateMessage& current,
            const protocol::PlayerActorStateMessage& delta);

        static void ClearTransitionTiming(
            protocol::PlayerActorStateMessage& message) noexcept;
        static void ClearStructuralTiming(
            protocol::PlayerActorStateMessage& message) noexcept;
    };
}
