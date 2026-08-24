#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Combat/ResolvedHitEvent.h"
#include "Multiplayer/Combat/CombatActionLedger.h"
#include "Multiplayer/Combat/CombatHitApplicator.h"
#include "Multiplayer/Combat/CombatHitCandidateBuilder.h"
#include "Multiplayer/Protocol/CombatHitMessage.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Replication/CombatHitAuthorityPipeline.h"
#include "Multiplayer/Replication/CombatHitDeliveryState.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::multiplayer
{
    class UdpPeer;
}

namespace fable::multiplayer::authority
{
    class AuthorityReplication;
}

namespace fable::multiplayer::combat
{
    class PlayerCombatantDirectory;
}

namespace fable::multiplayer::entities
{
    class EntityLifecycleReplication;
    class EntityNetworkIdentityRegistry;
    class EntityPresenceReplication;
}

namespace fable::multiplayer::replication
{
    class LocalHeroReplication;
    class RemotePlayerChannels;

    // Converts a resolved retail OnHit into one host-validated outcome. The
    // packet is reliable and target-scoped; current health remains separately
    // coalesced by EntityVitalsReplication for late joiners.
    class CombatHitReplication final : public ReliableMessageSink
    {
    public:
        [[nodiscard]] ReliableMessageTypeSet HandledPacketTypes()
            const noexcept override
        {
            static constexpr protocol::PacketType types[] = {
                protocol::PacketType::CombatHit};
            return {types, sizeof(types) / sizeof(types[0])};
        }

        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            UdpPeer& transport,
            authority::AuthorityReplication& authority,
            entities::EntityLifecycleReplication& lifecycle,
            entities::EntityNetworkIdentityRegistry& identities,
            entities::EntityPresenceReplication& presence,
            LocalHeroReplication& localHero,
            RemotePlayerChannels& remotePlayers,
            combat::PlayerCombatantDirectory& combatants,
            combat::CombatActionLedger& ledger,
            game::creature::combat::CreatureCombatService& combat,
            const core::Diagnostics& diagnostics);
        bool Process();
        bool HandleReliableMessage(
            const TransportMessage& message) override;
        void Shutdown() noexcept;

    private:
        static constexpr std::size_t NativeEventCapacity = 1024;
        static constexpr std::size_t PendingCapacity = 2048;
        static constexpr std::uint64_t NativeResolutionGraceMilliseconds = 500;
        static constexpr std::uint64_t ReliableDependencyGraceMilliseconds =
            3'000;

        struct DeferredNative final
        {
            game::creature::combat::ResolvedHitEvent event;
            std::uint64_t queuedAt = 0;
            // Native observation is attempted at most once for the lifetime
            // of this captured event, including resolution retries.
            bool observationProcessed = false;
        };

        struct PendingInbound final
        {
            protocol::CombatHitMessage message;
            std::uint64_t sourceActorId = 0;
            std::uint64_t sourceConnectionNonce = 0;
            std::uint64_t queuedAt = 0;
            bool resultAdmissionRecorded = false;
            bool hostLocalResult = false;
        };

        enum class InboundSessionState : std::uint8_t
        {
            Current,
            Pending,
            Stale,
        };

        static void CaptureNative(
            void* context,
            const game::creature::combat::ResolvedHitEvent& event) noexcept;
        void EnqueueNative(
            const game::creature::combat::ResolvedHitEvent& event) noexcept;
        bool ProcessNative(std::uint64_t now);
        bool ProcessInbound(std::uint64_t now);
        void RefreshTransportSession() noexcept;
        [[nodiscard]] InboundSessionState ClassifyInboundSession(
            const PendingInbound& pending) const noexcept;
        bool Queue(protocol::CombatHitMessage message);
        bool QueueDeferredResult(
            const CombatHitDeferredResult& deferredResult);
        bool PublishPending();

        UdpPeer* transport_ = nullptr;
        authority::AuthorityReplication* authority_ = nullptr;
        entities::EntityLifecycleReplication* lifecycle_ = nullptr;
        entities::EntityNetworkIdentityRegistry* identities_ = nullptr;
        entities::EntityPresenceReplication* presence_ = nullptr;
        LocalHeroReplication* localHero_ = nullptr;
        RemotePlayerChannels* remotePlayers_ = nullptr;
        combat::PlayerCombatantDirectory* combatants_ = nullptr;
        combat::CombatActionLedger* ledger_ = nullptr;
        game::creature::combat::CreatureCombatService* combat_ = nullptr;
        combat::CombatHitCandidateBuilder candidateBuilder_;
        combat::CombatHitApplicator applicator_;
        CombatHitAuthorityPipeline authorityPipeline_;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::uint64_t knownPeerRevision_ = 0;
        std::uint64_t localConnectionNonce_ = 0;
        std::uint64_t remoteAuthorityConnectionNonce_ = 0;
        std::mutex nativeMutex_;
        std::deque<game::creature::combat::ResolvedHitEvent> nativeEvents_;
        std::deque<DeferredNative> deferredNative_;
        std::deque<PendingInbound> inbound_;
        CombatHitPublicationQueue pendingPublications_;
        CombatHitResultRevisionCache appliedRevisions_;
        std::atomic_bool acceptingNative_{false};
        std::atomic_uint droppedNative_{0};
        unsigned int reportedDroppedNative_ = 0;
        bool publishBackpressured_ = false;
        bool initialized_ = false;
    };
}
