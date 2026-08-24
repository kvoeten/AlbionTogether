#pragma once

#include "Game/Math/Vector3.h"

#include <cstdint>

namespace fable::multiplayer::protocol
{
    enum class CombatHitPhase : std::uint8_t
    {
        Candidate = 1,
        Result = 2,
    };

    enum class CombatActionDomain : std::uint8_t
    {
        Player = 1,
        WorldEntity = 2,
    };

    enum class CombatParticipantKind : std::uint8_t
    {
        Player = 1,
        WorldEntity = 2,
    };

    namespace combat_hit_reaction_flag
    {
        inline constexpr std::uint32_t KnockDown = 1u << 0;
        inline constexpr std::uint32_t Decapitate = 1u << 1;
        inline constexpr std::uint32_t Blockable = 1u << 2;
        inline constexpr std::uint32_t Flourish = 1u << 3;
        inline constexpr std::uint32_t EpicSpell = 1u << 4;
        inline constexpr std::uint32_t BlockCounter = 1u << 5;
        inline constexpr std::uint32_t PlayHitResponse = 1u << 6;
        inline constexpr std::uint32_t PlayHitResponseOverrideSet = 1u << 7;
        inline constexpr std::uint32_t MoveBack = 1u << 8;
        inline constexpr std::uint32_t CreateParticleEffectOnHit = 1u << 9;
        inline constexpr std::uint32_t CreateDustParticleEffectOnHit =
            1u << 10;
        inline constexpr std::uint32_t GuaranteeHit = 1u << 11;
        inline constexpr std::uint32_t Blocked = 1u << 12;
        inline constexpr std::uint32_t HitNegated = 1u << 13;
        inline constexpr std::uint32_t CauseRecoil = 1u << 14;
        inline constexpr std::uint32_t Critical = 1u << 15;
        inline constexpr std::uint32_t Killed = 1u << 16;
        inline constexpr std::uint32_t Healing = 1u << 17;
        inline constexpr std::uint32_t HasReactionId = 1u << 18;
        inline constexpr std::uint32_t All = KnockDown | Decapitate |
            Blockable | Flourish | EpicSpell | BlockCounter |
            PlayHitResponse | PlayHitResponseOverrideSet | MoveBack |
            CreateParticleEffectOnHit | CreateDustParticleEffectOnHit |
            GuaranteeHit | Blocked | HitNegated | CauseRecoil | Critical |
            Killed | Healing | HasReactionId;
    }

    namespace combat_hit_impact_flag
    {
        inline constexpr std::uint8_t HasPosition = 1u << 0;
        inline constexpr std::uint8_t HasDirection = 1u << 1;
        inline constexpr std::uint8_t All = HasPosition | HasDirection;
    }

    // One resolved native hit. Candidate is authored only by the process that
    // simulates the exact source action. The target may be a protected remote
    // presentation. The host validates both lifecycles and returns Result with
    // ordered host/vitals revisions. Connection identity remains transport
    // metadata rather than payload state.
    struct CombatHitMessage final
    {
        CombatHitPhase phase = CombatHitPhase::Candidate;
        CombatActionDomain sourceDomain = CombatActionDomain::Player;
        CombatParticipantKind targetKind = CombatParticipantKind::Player;
        std::uint8_t impactFlags = 0;
        std::uint32_t reactionFlags = 0;

        std::uint64_t sourceActionId = 0;
        // Player actor ID for Player actions; canonical Thing UID for entity
        // actions.
        std::uint64_t sourceId = 0;
        // Player actions are owned by their actor. Entity actions retain the
        // actor that holds their map/action simulation authority.
        std::uint64_t sourceOwnerActorId = 0;
        std::uint32_t sourceAuthorityEpoch = 0;
        std::uint32_t sourceGeneration = 0;
        std::uint32_t sourceMapEpoch = 0;
        std::uint32_t sourceActionEpoch = 0;
        std::uint16_t sourceMapId = 0;

        // Player actor ID or canonical Thing UID according to targetKind.
        std::uint64_t targetId = 0;
        std::uint32_t targetAuthorityEpoch = 0;
        std::uint32_t targetGeneration = 0;
        std::uint32_t targetMapEpoch = 0;
        std::uint16_t targetMapId = 0;

        // Monotonic within one resolver transport session and exact target
        // incarnation. hitOrdinal distinguishes bounded multi-hit actions.
        std::uint64_t candidateSequence = 0;
        std::uint32_t hitOrdinal = 0;
        // Result-only host sequence for this exact target lifecycle.
        std::uint64_t hostTargetRevision = 0;

        // Outcome snapshots classify the semantic response. Current health
        // and death are owned and applied only by EntityVitalsReplication.
        float healthBefore = 0.0f;
        float healthAfter = 0.0f;
        float maximumHealth = 0.0f;
        // Optional stable native/semantic reaction identity. It is present
        // exactly when HasReactionId is set.
        std::uint32_t reactionId = 0;
        game::Vector3 impactPosition = {};
        game::Vector3 impactDirection = {};
        // Actor whose authoritative simulation resolved the Candidate. This
        // lets that process accept Result without replaying the native hit.
        std::uint64_t resolverActorId = 0;
    };
}
