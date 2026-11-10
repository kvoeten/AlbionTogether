#pragma once

#include "Multiplayer/Protocol/EntityActionMessage.h"

#include <cstdint>
#include <string>

namespace fable::multiplayer::replication::entity_actions
{
    // Runtime state for one authority-fenced entity action. Keeping this
    // record beside the entity-action pipeline makes its lifecycle explicit
    // without exposing native Fable pointers through the wire protocol.
    struct ActiveAction final
    {
        std::uint64_t entityUid = 0;
        std::uint32_t entityGeneration = 0;
        std::uint64_t actionId = 0;
        std::uint64_t ownerActorId = 0;
        std::uint64_t targetEntityUid = 0;
        std::uint32_t targetEntityGeneration = 0;
        std::uint64_t targetPlayerActorId = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t actionEpoch = 0;
        std::uint32_t abilityId = 0;
        float abilityCharge = 0.0f;
        // Correlates the semantic UseAbility request with the concrete action
        // accepted by SubmitAction. The semantic source and native owner may
        // legitimately differ when the local Hero routes combat to a puppet.
        std::uint64_t actionCorrelationId = 0;
        std::uint64_t nativeThingUid = 0;
        protocol::EntityActionKind kind = protocol::EntityActionKind::Native;
        std::uint8_t flags = 0;
        std::string mapName;
        std::string semanticName;
        void* nativeAction = nullptr;
        std::uint64_t lastActivityAt = 0;
        bool localOrigin = false;
        bool combatEngagement = false;
        bool finished = false;
        bool endQueued = false;
        bool pendingRelease = false;
        // A combat sub-action may borrow its actor's already ordered
        // PrimaryAttacker lease. Ending that sub-action must not release the
        // engagement that owns the lease.
        bool ownsLease = true;
        bool nativeAbility = false;
        bool abilityReplayed = false;
        bool abilityReplayFailed = false;
        bool nativeReplayed = false;
        bool nativeReplayFailed = false;
        std::uint32_t nativeReplayAttempts = 0;
        std::uint64_t nextNativeReplayAt = 0;

        [[nodiscard]] bool MatchesAbilitySubmission(
            const std::uint64_t correlationId,
            const std::uint64_t submittedThingUid) const noexcept
        {
            if (!localOrigin || !nativeAbility || nativeAction != nullptr)
            {
                return false;
            }
            if (actionCorrelationId != 0 || correlationId != 0)
            {
                return actionCorrelationId != 0 &&
                    actionCorrelationId == correlationId;
            }
            return entityUid != 0 && entityUid == submittedThingUid;
        }
    };
}
