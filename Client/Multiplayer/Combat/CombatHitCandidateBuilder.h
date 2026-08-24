#pragma once

#include "Game/Creature/Combat/ResolvedHitEvent.h"
#include "Multiplayer/Combat/CombatActionLedger.h"
#include "Multiplayer/Protocol/CombatHitMessage.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace fable::multiplayer::authority
{
    class AuthorityReplication;
}

namespace fable::multiplayer::entities
{
    class EntityLifecycleReplication;
    class EntityNetworkIdentityRegistry;
}

namespace fable::multiplayer::replication
{
    class LocalHeroReplication;
    class RemotePlayerChannels;
}

namespace fable::multiplayer::combat
{
    class PlayerCombatantDirectory;

    // Correlates one native OnHit with the source action that was active when
    // the retail game resolved it, then builds a pointer-free Candidate.
    class CombatHitCandidateBuilder final
    {
    public:
        void Initialize(
            std::uint64_t localActorId,
            authority::AuthorityReplication& authority,
            entities::EntityLifecycleReplication& lifecycle,
            entities::EntityNetworkIdentityRegistry& identities,
            replication::LocalHeroReplication& localHero,
            replication::RemotePlayerChannels& remotePlayers,
            PlayerCombatantDirectory& combatants,
            CombatActionLedger& ledger) noexcept;
        bool Build(
            const game::creature::combat::ResolvedHitEvent& event,
            protocol::CombatHitMessage& candidate);
        void Clear() noexcept;

    private:
        static constexpr std::size_t MaximumOrdinalEntries = 256;

        struct Participant final
        {
            CombatLifecycle lifecycle;
            std::uint32_t authorityEpoch = 0;
            std::uint16_t mapId = 0;
            std::uint64_t ownerActorId = 0;
        };

        struct SourceKey final
        {
            CombatSourceAction action;
            bool operator==(const SourceKey& other) const noexcept
            {
                return action == other.action;
            }
        };

        struct SourceHash final
        {
            std::size_t operator()(const SourceKey& key) const noexcept;
        };

        struct OrdinalRecord final
        {
            std::uint32_t ordinal = 0;
            std::uint64_t serial = 0;
        };

        [[nodiscard]] bool ResolveParticipant(
            std::uint64_t localThingUid,
            Participant& participant) const noexcept;
        [[nodiscard]] std::uint32_t NextOrdinal(
            const CombatSourceAction& action) noexcept;
        void EvictOldestOrdinal() noexcept;
        [[nodiscard]] static std::uint32_t ReactionFlags(
            const game::creature::combat::ResolvedHitEvent& event) noexcept;

        authority::AuthorityReplication* authority_ = nullptr;
        entities::EntityLifecycleReplication* lifecycle_ = nullptr;
        entities::EntityNetworkIdentityRegistry* identities_ = nullptr;
        replication::LocalHeroReplication* localHero_ = nullptr;
        replication::RemotePlayerChannels* remotePlayers_ = nullptr;
        PlayerCombatantDirectory* combatants_ = nullptr;
        CombatActionLedger* ledger_ = nullptr;
        std::unordered_map<SourceKey, OrdinalRecord, SourceHash> ordinals_;
        std::uint64_t localActorId_ = 0;
        std::uint64_t nextCandidateSequence_ = 0;
        std::uint64_t nextSerial_ = 0;
    };
}
