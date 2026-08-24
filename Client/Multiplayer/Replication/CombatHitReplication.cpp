#include "CombatHitReplication.h"

#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Entities/EntityLifecycleReplication.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/EntityPresenceReplication.h"
#include "Multiplayer/Entities/WorldEntityDirectory.h"
#include "Multiplayer/Protocol/CombatHitCodec.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"
#include "Multiplayer/Runtime/MultiplayerSessionContexts.h"
#include "Multiplayer/Transport/ReliableSinkDescriptorRegistry.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <utility>

namespace fable::multiplayer::replication
{
    void CombatHitReplication::Initialize(
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
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        transport_ = &transport;
        authority_ = &authority;
        lifecycle_ = &lifecycle;
        identities_ = &identities;
        presence_ = &presence;
        localHero_ = &localHero;
        remotePlayers_ = &remotePlayers;
        combatants_ = &combatants;
        ledger_ = &ledger;
        combat_ = &combat;
        diagnostics_ = diagnostics;
        knownPeerRevision_ = transport.PeerSetRevision();
        localConnectionNonce_ = transport.ConnectionNonce();
        candidateBuilder_.Initialize(
            localActorId, authority, lifecycle, identities, localHero,
            remotePlayers, combatants, ledger);
        applicator_.Initialize(
            localActorId, combatants, lifecycle, identities, presence, localHero,
            remotePlayers, combat, diagnostics);
        authorityPipeline_.Initialize(
            role,
            localActorId,
            authority,
            lifecycle,
            localHero,
            remotePlayers,
            ledger,
            applicator_,
            pendingPublications_,
            appliedRevisions_,
            diagnostics);
        if (!combat.AddResolvedHitSink(
                &CombatHitReplication::CaptureNative, this))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-combat-hit-observer");
            Shutdown();
            return;
        }
        acceptingNative_.store(true, std::memory_order_release);
        initialized_ = true;
        diagnostics_.Event(
            "MultiplayerCombatHitReady",
            "native OnHit outcomes use reliable target-scoped host validation");
    }

    void CombatHitReplication::RefreshTransportSession() noexcept
    {
        if (transport_ == nullptr || role_ != PeerRole::Guest)
        {
            return;
        }
        const std::uint64_t peerRevision = transport_->PeerSetRevision();
        const std::uint64_t localNonce = transport_->ConnectionNonce();
        const bool peerChanged = peerRevision != knownPeerRevision_ &&
            (peerRevision != 0 || knownPeerRevision_ != 0);
        const bool localSessionChanged = localNonce != localConnectionNonce_ &&
            (localNonce != 0 || localConnectionNonce_ != 0);
        knownPeerRevision_ = peerRevision;
        localConnectionNonce_ = localNonce;
        if (!peerChanged && !localSessionChanged)
        {
            return;
        }

        inbound_.clear();
        pendingPublications_.Clear();
        deferredNative_.clear();
        appliedRevisions_.Clear();
        applicator_.ClearNativeHitObservations();
        remoteAuthorityConnectionNonce_ = 0;
        publishBackpressured_ = false;
        diagnostics_.Event(
            "MultiplayerCombatHitSessionFenced",
            "transport session changed; stale hit dependencies and result revisions were retired");
    }

    CombatHitReplication::InboundSessionState
        CombatHitReplication::ClassifyInboundSession(
            const PendingInbound& pending) const noexcept
    {
        if (pending.sourceConnectionNonce == 0)
        {
            return InboundSessionState::Stale;
        }
        if (role_ == PeerRole::Guest)
        {
            return remoteAuthorityConnectionNonce_ == 0
                ? InboundSessionState::Pending
                : (pending.sourceConnectionNonce ==
                        remoteAuthorityConnectionNonce_
                    ? InboundSessionState::Current
                    : InboundSessionState::Stale);
        }
        if (pending.sourceActorId == localActorId_)
        {
            return pending.sourceConnectionNonce == localConnectionNonce_
                ? InboundSessionState::Current
                : InboundSessionState::Stale;
        }
        const RemotePlayerLifecycle* const lifecycle =
            remotePlayers_ != nullptr
            ? remotePlayers_->FindLifecycle(pending.sourceActorId)
            : nullptr;
        if (lifecycle == nullptr || lifecycle->connectionNonce == 0)
        {
            return InboundSessionState::Pending;
        }
        if (!lifecycle->active)
        {
            return InboundSessionState::Stale;
        }
        return lifecycle->connectionNonce == pending.sourceConnectionNonce
            ? InboundSessionState::Current
            : InboundSessionState::Stale;
    }

    void CombatHitReplication::CaptureNative(
        void* context,
        const game::creature::combat::ResolvedHitEvent& event) noexcept
    {
        if (context != nullptr)
        {
            static_cast<CombatHitReplication*>(context)->EnqueueNative(event);
        }
    }

    void CombatHitReplication::EnqueueNative(
        const game::creature::combat::ResolvedHitEvent& event) noexcept
    {
        if (!acceptingNative_.load(std::memory_order_acquire))
        {
            return;
        }
        std::lock_guard<std::mutex> lock(nativeMutex_);
        if (nativeEvents_.size() >= NativeEventCapacity)
        {
            droppedNative_.fetch_add(1, std::memory_order_acq_rel);
            return;
        }
        nativeEvents_.push_back(event);
    }

    bool CombatHitReplication::ProcessNative(std::uint64_t now)
    {
        std::deque<game::creature::combat::ResolvedHitEvent> captured;
        {
            std::lock_guard<std::mutex> lock(nativeMutex_);
            captured.swap(nativeEvents_);
        }
        std::deque<DeferredNative> retries;
        while (!deferredNative_.empty())
        {
            retries.push_front(std::move(deferredNative_.back()));
            deferredNative_.pop_back();
        }

        const auto process = [this, now](
                                 const game::creature::combat::ResolvedHitEvent& event,
                                 bool observationProcessed) -> bool
        {
            if (!observationProcessed)
            {
                observationProcessed = applicator_.ObserveNativeHit(event);
            }
            protocol::CombatHitMessage candidate;
            if (!candidateBuilder_.Build(event, candidate))
            {
                if (now >= event.observedAt &&
                        now - event.observedAt <=
                        NativeResolutionGraceMilliseconds &&
                    deferredNative_.size() < NativeEventCapacity)
                {
                    deferredNative_.push_back(
                        {event, now, observationProcessed});
                }
                return true;
            }
            if (role_ == PeerRole::Host)
            {
                const CombatHitAdmissionOutcome admission =
                    authorityPipeline_.AcceptCandidate(
                        candidate,
                        localActorId_,
                        localConnectionNonce_);
                if (!admission.Succeeded())
                {
                    return false;
                }
                if (admission.hasDeferredResult &&
                    !QueueDeferredResult(admission.deferredResult))
                {
                    return false;
                }
                if (admission.Deferred() &&
                    deferredNative_.size() < NativeEventCapacity)
                {
                    deferredNative_.push_back(
                        {event, now, observationProcessed});
                }
            }
            else if (!Queue(std::move(candidate)))
            {
                return false;
            }
            return true;
        };

        for (const auto& event : captured)
        {
            if (!process(event, false))
            {
                return false;
            }
        }
        for (const auto& retry : retries)
        {
            if (!process(retry.event, retry.observationProcessed))
            {
                return false;
            }
        }
        return true;
    }

    bool CombatHitReplication::ProcessInbound(std::uint64_t now)
    {
        const std::size_t scheduled = inbound_.size();
        for (std::size_t index = 0; index < scheduled; ++index)
        {
            PendingInbound pending = std::move(inbound_.front());
            inbound_.pop_front();
            const InboundSessionState session =
                ClassifyInboundSession(pending);
            if (session == InboundSessionState::Stale)
            {
                continue;
            }
            bool defer = false;
            bool accepted = true;
            if (session == InboundSessionState::Pending)
            {
                defer = true;
            }
            else if (pending.hostLocalResult)
            {
                const CombatHitAdmissionOutcome result =
                    authorityPipeline_.AcceptResult(
                    pending.message,
                    pending.sourceConnectionNonce,
                    true,
                    pending.resultAdmissionRecorded);
                accepted = result.Succeeded();
                defer = result.Deferred();
            }
            else if (role_ == PeerRole::Host)
            {
                const CombatHitAdmissionOutcome admission =
                    authorityPipeline_.AcceptCandidate(
                    pending.message,
                    pending.sourceActorId,
                    pending.sourceConnectionNonce);
                accepted = admission.Succeeded();
                defer = admission.Deferred();
                if (accepted && admission.hasDeferredResult &&
                    !QueueDeferredResult(admission.deferredResult))
                {
                    return false;
                }
            }
            else
            {
                const CombatHitAdmissionOutcome result =
                    authorityPipeline_.AcceptResult(
                    pending.message,
                    pending.sourceConnectionNonce,
                    false,
                    pending.resultAdmissionRecorded);
                accepted = result.Succeeded();
                defer = result.Deferred();
            }
            if (!accepted)
            {
                return false;
            }
            if (defer && now >= pending.queuedAt &&
                now - pending.queuedAt <=
                    ReliableDependencyGraceMilliseconds &&
                inbound_.size() < PendingCapacity)
            {
                inbound_.push_back(std::move(pending));
            }
        }
        return true;
    }

    bool CombatHitReplication::Process()
    {
        if (!initialized_)
        {
            return false;
        }
        RefreshTransportSession();
        const std::uint64_t now = GetTickCount64();
        if (!ProcessNative(now) || !ProcessInbound(now) || !PublishPending())
        {
            return false;
        }
        const unsigned int dropped = droppedNative_.load(
            std::memory_order_acquire);
        if (dropped != reportedDroppedNative_)
        {
            reportedDroppedNative_ = dropped;
            diagnostics_.Event(
                "MultiplayerCombatHitOverflow",
                "native resolved hits exceeded the bounded queue");
            return false;
        }
        return true;
    }

    bool CombatHitReplication::HandleReliableMessage(
        const TransportMessage& transportMessage)
    {
        if (!initialized_ ||
            transportMessage.type != protocol::PacketType::CombatHit)
        {
            return false;
        }
        RefreshTransportSession();
        protocol::CombatHitMessage message;
        if (!protocol::DecodeCombatHitMessage(
                transportMessage.payload.data(),
                transportMessage.payloadSize,
                message))
        {
            diagnostics_.Event(
                "MultiplayerCombatHitRejected", "invalid combat hit payload");
            return true;
        }
        if ((role_ == PeerRole::Host &&
                message.phase != protocol::CombatHitPhase::Candidate) ||
            (role_ == PeerRole::Guest &&
                message.phase != protocol::CombatHitPhase::Result))
        {
            diagnostics_.Event(
                "MultiplayerCombatHitRejected",
                "combat hit phase did not match sender authority");
            return true;
        }
        if (transportMessage.connectionNonce == 0)
        {
            diagnostics_.Event(
                "MultiplayerCombatHitRejected",
                "combat hit had no transport-session identity");
            return true;
        }
        if (role_ == PeerRole::Guest)
        {
            if (remoteAuthorityConnectionNonce_ == 0)
            {
                remoteAuthorityConnectionNonce_ =
                    transportMessage.connectionNonce;
            }
            else if (remoteAuthorityConnectionNonce_ !=
                transportMessage.connectionNonce)
            {
                diagnostics_.Event(
                    "MultiplayerCombatHitRejected",
                    "combat result was from a stale authority session");
                return true;
            }
        }
        if (inbound_.size() >= PendingCapacity)
        {
            diagnostics_.Event(
                "MultiplayerCombatHitOverflow",
                "reliable combat hit dependency queue is full");
            return false;
        }
        inbound_.push_back({
            std::move(message),
            transportMessage.sourceActorId,
            transportMessage.connectionNonce,
            GetTickCount64(),
            false,
            false});
        return true;
    }

    bool CombatHitReplication::QueueDeferredResult(
        const CombatHitDeferredResult& deferredResult)
    {
        if (inbound_.size() >= PendingCapacity)
        {
            diagnostics_.Event(
                "MultiplayerCombatHitOverflow",
                "host-local result dependency queue is full");
            return false;
        }
        inbound_.push_back({
            deferredResult.message,
            deferredResult.sourceActorId,
            deferredResult.sourceConnectionNonce,
            GetTickCount64(),
            deferredResult.resultAdmissionRecorded,
            deferredResult.hostLocalResult});
        return true;
    }

    bool CombatHitReplication::Queue(protocol::CombatHitMessage message)
    {
        if (!pendingPublications_.Enqueue(std::move(message)))
        {
            diagnostics_.Event(
                "MultiplayerCombatHitOverflow",
                "bounded combat hit publication queue is full");
            return false;
        }
        return true;
    }

    bool CombatHitReplication::PublishPending()
    {
        bool deferred = false;
        const bool drained = pendingPublications_.DrainRound(
            [this](
                const protocol::CombatHitMessage& message,
                const ReliableStreamId stream)
                -> CombatHitPublicationAttempt
        {
            std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload = {};
            std::size_t payloadSize = 0;
            if (!protocol::EncodeCombatHitMessage(
                    message,
                    payload.data(),
                    protocol::MaximumPayloadBytes(),
                    payloadSize))
            {
                return CombatHitPublicationAttempt::Failed;
            }
            if (!transport_->SubmitReliable(
                    stream,
                    protocol::PacketType::CombatHit,
                    payload.data(),
                    payloadSize))
            {
                if (transport_->HasFailed())
                {
                    return CombatHitPublicationAttempt::Failed;
                }
                return CombatHitPublicationAttempt::Deferred;
            }
            return CombatHitPublicationAttempt::Submitted;
        },
            deferred);
        if (!drained)
        {
            return false;
        }
        if (deferred && !publishBackpressured_)
        {
            diagnostics_.Event(
                "MultiplayerCombatHitPublishDeferred",
                "one or more target streams are waiting for reliable transport capacity");
            publishBackpressured_ = true;
        }
        else if (!deferred && publishBackpressured_)
        {
            diagnostics_.Event(
                "MultiplayerCombatHitPublishResumed",
                "queued hit outcomes entered the target stream");
            publishBackpressured_ = false;
        }
        return true;
    }

    void CombatHitReplication::Shutdown() noexcept
    {
        acceptingNative_.store(false, std::memory_order_release);
        if (combat_ != nullptr)
        {
            combat_->RemoveResolvedHitSink(
                &CombatHitReplication::CaptureNative, this);
        }
        {
            std::lock_guard<std::mutex> lock(nativeMutex_);
            nativeEvents_.clear();
        }
        deferredNative_.clear();
        inbound_.clear();
        pendingPublications_.Clear();
        appliedRevisions_.Clear();
        candidateBuilder_.Clear();
        authorityPipeline_.Shutdown();
        applicator_.Shutdown();
        transport_ = nullptr;
        authority_ = nullptr;
        lifecycle_ = nullptr;
        identities_ = nullptr;
        presence_ = nullptr;
        localHero_ = nullptr;
        remotePlayers_ = nullptr;
        combatants_ = nullptr;
        ledger_ = nullptr;
        combat_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        knownPeerRevision_ = 0;
        localConnectionNonce_ = 0;
        remoteAuthorityConnectionNonce_ = 0;
        droppedNative_.store(0, std::memory_order_release);
        reportedDroppedNative_ = 0;
        publishBackpressured_ = false;
        initialized_ = false;
    }
}

namespace
{
    fable::multiplayer::ReliableMessageSink* ResolveCombatHitSink(
        fable::multiplayer::MultiplayerSessionContexts& contexts) noexcept
    {
        return &contexts.actions.combatHits;
    }
}

FABLE_RELIABLE_SINK_DESCRIPTOR(
    g_fableReliableSinkCombatHit,
    0x100Au,
    "combat-hit",
    550u,
    "multiplayer-combat-hit-dispatch",
    ResolveCombatHitSink);
