#pragma once

#include "Multiplayer/Combat/CombatHitApplicator.h"
#include "Multiplayer/Protocol/CombatHitMessage.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Replication/CombatHitDeliveryState.h"

#include <cstdint>

namespace fable::multiplayer::authority
{
    class AuthorityReplication;
}

namespace fable::multiplayer::entities
{
    class EntityLifecycleReplication;
}

namespace fable::multiplayer::replication
{
    class LocalHeroReplication;
    class RemotePlayerChannels;

    struct CombatHitDeferredResult final
    {
        protocol::CombatHitMessage message;
        std::uint64_t sourceActorId = 0;
        std::uint64_t sourceConnectionNonce = 0;
        bool resultAdmissionRecorded = false;
        bool hostLocalResult = false;
    };

    enum class CombatHitAdmissionDisposition : std::uint8_t
    {
        Consumed,
        Deferred,
        Failed,
    };

    struct CombatHitAdmissionOutcome final
    {
        CombatHitAdmissionDisposition disposition =
            CombatHitAdmissionDisposition::Consumed;
        bool admissionRecorded = false;
        bool hasDeferredResult = false;
        CombatHitDeferredResult deferredResult;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return disposition != CombatHitAdmissionDisposition::Failed;
        }

        [[nodiscard]] bool Deferred() const noexcept
        {
            return disposition == CombatHitAdmissionDisposition::Deferred;
        }
    };

    // Owns semantic hit admission and result application. Transport queues,
    // native event capture, and reliable publication remain with the session
    // coordinator so this service has no socket or thread responsibilities.
    class CombatHitAuthorityPipeline final
    {
    public:
        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            authority::AuthorityReplication& authority,
            entities::EntityLifecycleReplication& lifecycle,
            LocalHeroReplication& localHero,
            RemotePlayerChannels& remotePlayers,
            combat::CombatActionLedger& ledger,
            combat::CombatHitApplicator& applicator,
            CombatHitPublicationQueue& pendingPublications,
            CombatHitResultRevisionCache& appliedRevisions,
            const core::Diagnostics& diagnostics) noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] CombatHitAdmissionOutcome AcceptCandidate(
            protocol::CombatHitMessage candidate,
            std::uint64_t sourceActorId,
            std::uint64_t sourceConnectionNonce);
        [[nodiscard]] CombatHitAdmissionOutcome AcceptResult(
            const protocol::CombatHitMessage& result,
            std::uint64_t authorityConnectionNonce,
            bool admissionAlreadyRecorded,
            bool& admissionRecorded);

    private:
        [[nodiscard]] bool ValidateResolverAuthority(
            const protocol::CombatHitMessage& candidate,
            std::uint64_t sourceActorId) const noexcept;
        [[nodiscard]] bool IsTargetCurrent(
            const protocol::CombatHitMessage& message) const noexcept;
        [[nodiscard]] bool IsSourceCurrent(
            const protocol::CombatHitMessage& message) const noexcept;

        authority::AuthorityReplication* authority_ = nullptr;
        entities::EntityLifecycleReplication* lifecycle_ = nullptr;
        LocalHeroReplication* localHero_ = nullptr;
        RemotePlayerChannels* remotePlayers_ = nullptr;
        combat::CombatActionLedger* ledger_ = nullptr;
        combat::CombatHitApplicator* applicator_ = nullptr;
        CombatHitPublicationQueue* pendingPublications_ = nullptr;
        CombatHitResultRevisionCache* appliedRevisions_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
    };
}
