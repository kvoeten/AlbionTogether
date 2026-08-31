#pragma once

#include "Multiplayer/Protocol/PlayerActorStateMessage.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Replication/PlayerActorLifecycleLimits.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fable::multiplayer::replication
{
    // The lifecycle key is deliberately separate from PlayerState.  Movement
    // is lossy and may be dropped or reordered; these values are established
    // only by the reliable actor-state stream.
    struct RemotePlayerLifecycle final
    {
        std::uint32_t actorGeneration = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t structuralRevision = 0;
        std::uint64_t connectionNonce = 0;
        bool appearancePresent = false;
        bool equipmentPresent = false;
        bool appearanceReady = false;
        bool equipmentReady = false;
        bool active = false;
    };

    // Current equipment RepNotify presentation for one actor incarnation.
    // This is one replace-in-place value, never an animation history queue.
    struct RemoteEquipmentTransition final
    {
        std::uint64_t actionId = 0;
        std::uint64_t startedAtLocalMs = 0;
        std::uint32_t animationId = 0;
        std::uint16_t durationMs = 0;
        std::uint16_t attachmentNotifyOffsetMs = 0;

        [[nodiscard]] bool IsPresent() const noexcept
        {
            return actionId != 0 && startedAtLocalMs != 0 &&
                animationId != 0 && durationMs != 0 &&
                attachmentNotifyOffsetMs <= durationMs;
        }
    };

    struct RemotePlayerSnapshot final
    {
        PlayerState state;
        std::uint64_t receivedAt = 0;
        RemotePlayerLifecycle lifecycle;
        RemoteEquipmentTransition equipmentTransition;

        [[nodiscard]] std::uint32_t ActorGeneration() const noexcept
        {
            return lifecycle.actorGeneration;
        }

        [[nodiscard]] std::uint32_t MapEpoch() const noexcept
        {
            return lifecycle.mapEpoch;
        }

        [[nodiscard]] std::uint32_t StructuralRevision() const noexcept
        {
            return lifecycle.structuralRevision;
        }

        [[nodiscard]] bool AppearanceReady() const noexcept
        {
            return lifecycle.appearanceReady;
        }

        [[nodiscard]] bool EquipmentReady() const noexcept
        {
            return lifecycle.equipmentReady;
        }
    };

    // Current state is keyed by actor lifecycle. This is not snapshot history:
    // every live remote player occupies one replace-in-place channel entry.
    class RemotePlayerChannels final
    {
    public:
        // Reliable structural lifecycle. Construct is idempotent for the
        // same incarnation/revision; ComponentDelta revisions are ordered.
        bool ApplyActorState(
            const protocol::PlayerActorStateMessage& message,
            std::uint64_t receivedAt,
            std::uint64_t connectionNonce = 0,
            protocol::SessionTimeMs sessionNow =
                protocol::SessionTimeUnset);
        bool Apply(const PlayerState& update, std::uint64_t receivedAt);
        [[nodiscard]] std::vector<RemotePlayerSnapshot> Snapshots() const;
        [[nodiscard]] const PlayerState* Find(
            std::uint64_t actorId) const noexcept;
        [[nodiscard]] const RemotePlayerLifecycle* FindLifecycle(
            std::uint64_t actorId) const noexcept;
        [[nodiscard]] bool IsLifecycleActive(
            std::uint64_t actorId,
            std::uint32_t actorGeneration,
            std::uint32_t mapEpoch) const noexcept;
        [[nodiscard]] bool IsAppearanceReady(
            std::uint64_t actorId,
            std::uint32_t actorGeneration,
            std::uint32_t mapEpoch) const noexcept;
        [[nodiscard]] bool IsEquipmentReady(
            std::uint64_t actorId,
            std::uint32_t actorGeneration,
            std::uint32_t mapEpoch) const noexcept;
        void Remove(std::uint64_t actorId) noexcept;
        void Clear() noexcept;
        void ConsumeInvalidations(
            std::vector<std::uint64_t>& actorIds,
            bool& allActors) noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;
        // Advances only when a complete remote actor incarnation becomes a
        // viable network observer, or when that observer changes maps. Systems
        // with lossy current-state lanes can use it to publish one bounded
        // relevancy refresh after reliable construction has completed.
        [[nodiscard]] std::uint64_t ObserverReadinessRevision() const noexcept;

    private:
        static constexpr std::size_t MaxTrackedActors =
            player_actor_lifecycle::MaxTrackedActors;
        void AdvanceObserverReadinessRevision() noexcept;

        std::unordered_map<std::uint64_t, RemotePlayerSnapshot> channels_;
        std::unordered_set<std::uint64_t> invalidatedActors_;
        std::uint64_t observerReadinessRevision_ = 0;
        bool allActorsInvalidated_ = false;
    };
}
