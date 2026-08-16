#include "EntityLifecycleReplication.h"

#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Protocol/EntityLifecycleMessage.h"
#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Transport/TransportMessage.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_set>
#include <utility>

namespace fable::multiplayer::entities
{
    void EntityLifecycleReplication::Initialize(
        PeerRole role,
        std::uint64_t localActorId,
        UdpPeer& transport,
        authority::AuthorityReplication& authority,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        transport_ = &transport;
        authority_ = &authority;
        diagnostics_ = diagnostics;
        initialized_ = true;
        diagnostics_.Event(
            "MultiplayerEntityLifecycleReady",
            "host-issued Thing generations and bounded world baselines are active");
    }

    bool EntityLifecycleReplication::Reconcile(
        const LiveEntityRegistry& liveEntities,
        const std::vector<LiveEntityChange>& changes,
        bool baselineRequired,
        const std::string& localMap,
        std::uint16_t localMapId,
        bool ownerRosterReady)
    {
        if (!initialized_ || transport_ == nullptr || authority_ == nullptr)
        {
            return false;
        }

        const authority::MapAuthorityLease* const lease =
            authority_->FindMapLease(localMap);
        const bool ownsMap = lease != nullptr && !localMap.empty() &&
            lease->actorId == localActorId_ && lease->epoch != 0;
        const bool sameOwnedLease = ownsMap &&
            lastOwnedMap_ == localMap &&
            lastOwnedMapEpoch_ == lease->epoch;
        const bool ownershipChanged = ownsMap &&
            !sameOwnedLease;
        const bool canPublishOwnedMap = ownsMap && ownerRosterReady &&
            (role_ == PeerRole::Host ||
                directory_.HasAuthoritativeBaseline());
        // Native owner-authored additions and removals briefly make the live
        // roster differ from the canonical directory. Materialization detects
        // that difference before lifecycle reconciliation runs, so permit the
        // current incremental batch when this exact lease was verified ready
        // on the preceding reconciliation. New leases still require a full
        // ready roster before they can seed or mutate canonical state.
        const bool canPublishOwnedChanges = canPublishOwnedMap ||
            (sameOwnedLease && lastOwnedRosterReady_);

        if (canPublishOwnedMap && (baselineRequired || ownershipChanged) &&
            !QueueLocalSnapshot(
                liveEntities, localMap, localMapId, lease->epoch))
        {
            return false;
        }
        if (canPublishOwnedChanges)
        {
            // CTCMapwho can unregister and re-register one live Thing while
            // merely rebucketing or toggling its spatial component. Preserve
            // ordered events in the presence bridge so a real destination-map
            // transfer is never overwritten, but collapse a same-map
            // unregister when a later same-batch registration proves the
            // Thing never left this authority domain.
            std::vector<bool> skipSameMapUnregister(changes.size(), false);
            std::unordered_set<std::uint64_t> presentLater;
            for (std::size_t index = changes.size(); index != 0; --index)
            {
                const LiveEntityChange& change = changes[index - 1];
                const bool onLocalMap = localMapId == 0 ||
                    change.record.mapId == 0 ||
                    change.record.mapId == localMapId;
                if (change.kind == LiveEntityChangeKind::Unregistered)
                {
                    if (onLocalMap && presentLater.find(
                            change.record.thingUid) != presentLater.end())
                    {
                        skipSameMapUnregister[index - 1] = true;
                    }
                }
                else if (onLocalMap)
                {
                    presentLater.insert(change.record.thingUid);
                }
            }
            for (std::size_t index = 0; index < changes.size(); ++index)
            {
                if (skipSameMapUnregister[index])
                {
                    continue;
                }
                const LiveEntityChange& change = changes[index];
                if (!ObserveLocalChange(
                        change, localMap, localMapId, lease->epoch))
                {
                    return false;
                }
            }
        }
        if (ownsMap)
        {
            lastOwnedMap_ = localMap;
            lastOwnedMapEpoch_ = lease->epoch;
            lastOwnedRosterReady_ = ownerRosterReady;
        }
        else
        {
            lastOwnedMap_.clear();
            lastOwnedMapEpoch_ = 0;
            lastOwnedRosterReady_ = false;
        }

        if (role_ == PeerRole::Host && !ResolveUnknownMapNames())
        {
            return false;
        }
        if (role_ == PeerRole::Host && !HostReconcileMapAuthorities())
        {
            return false;
        }
        if (!FlushPendingTransfers())
        {
            return false;
        }

        if (role_ == PeerRole::Host)
        {
            const std::uint64_t peerRevision = transport_->PeerSetRevision();
            if (peerRevision != knownPeerRevision_ && !QueueBaseline())
            {
                return false;
            }
            knownPeerRevision_ = peerRevision;
        }
        return PublishPending();
    }

    bool EntityLifecycleReplication::ObserveLocalChange(
        const LiveEntityChange& change,
        const std::string& localMap,
        std::uint16_t localMapId,
        std::uint32_t mapEpoch)
    {
        if (!LiveEntityRegistry::IsReplicable(change.record))
        {
            return true;
        }
        if (change.kind != LiveEntityChangeKind::Unregistered &&
            localMapId != 0 && change.record.mapId != 0 &&
            change.record.mapId != localMapId)
        {
            // During a world transition destination registrations can arrive
            // while the source-map drain is still running. The destination
            // owner's first complete snapshot will publish them with the
            // correct map lease; never mislabel them as source-map entities.
            return true;
        }
        const bool present =
            change.kind != LiveEntityChangeKind::Unregistered;
        const bool transfer = !present && localMapId != 0 &&
            change.record.mapId != 0 && change.record.mapId != localMapId;
        if (role_ != PeerRole::Host)
        {
            protocol::EntityLifecycleMessage intent = LocalIntent(
                change.record,
                present,
                localMap,
                localMapId,
                mapEpoch);
            if (transfer && intent.entityGeneration == 0)
            {
                if (pendingTransfers_.size() >= PendingMessageCapacity &&
                    pendingTransfers_.find(intent.entityUid) ==
                        pendingTransfers_.end())
                {
                    return false;
                }
                pendingTransfers_[intent.entityUid] = std::move(intent);
                diagnostics_.Event(
                    "MultiplayerEntityTransferDeferred",
                    "waiting for the host-issued Thing generation before publishing a transfer");
                return true;
            }
            return Queue(std::move(intent));
        }

        protocol::EntityLifecycleMessage authoritative;
        bool changed = false;
        if (transfer)
        {
            // A creature can be constructed with its definition/home map ID
            // while never becoming part of the current live-map roster. Its
            // registration is intentionally ignored above, so a later
            // unregister must not be promoted into a cross-map transfer.
            // Only an entity which is currently canonical, live, and owned in
            // this exact source lease is eligible to cross the boundary.
            const WorldEntityRecord* const canonical =
                directory_.Find(change.record.thingUid);
            if (canonical == nullptr || !canonical->live ||
                !canonical->available ||
                canonical->mapName != localMap ||
                canonical->mapEpoch != mapEpoch ||
                canonical->simulationOwnerActorId != localActorId_ ||
                canonical->mapId == change.record.mapId)
            {
                diagnostics_.Event(
                    "MultiplayerEntityTransferIgnored",
                    "untracked or stale local Thing unregister was not a canonical cross-map transfer");
                return true;
            }
            const protocol::EntityLifecycleMessage intent = LocalIntent(
                change.record,
                false,
                localMap,
                localMapId,
                mapEpoch);
            if (!HostAcceptTransfer(
                    intent,
                    localActorId_,
                    authoritative,
                    changed))
            {
                diagnostics_.Event(
                    "MultiplayerEntityTransferRejected",
                    "host local Thing transfer was stale or invalid");
                // Native presence callbacks can race an already-applied
                // lifecycle update. A fenced local observation is stale
                // input, not a transport/session failure.
                return true;
            }
            return !changed || Queue(std::move(authoritative));
        }
        if (!present)
        {
            const WorldEntityRecord* const canonical =
                directory_.Find(change.record.thingUid);
            if (canonical != nullptr &&
                (canonical->mapName != localMap ||
                    canonical->mapEpoch != mapEpoch ||
                    canonical->simulationOwnerActorId != localActorId_))
            {
                diagnostics_.Event(
                    "MultiplayerEntityLifecycleStale",
                    "source-map unregister arrived after the canonical Thing moved to another authority domain");
                return true;
            }
        }
        if (!directory_.HostObserve(
                change,
                localMap,
                localActorId_,
                mapEpoch,
                authoritative,
                changed))
        {
            diagnostics_.Event(
                "MultiplayerEntityLifecycleRejected",
                "host local Thing observation was invalid");
            return false;
        }
        return !changed || Queue(std::move(authoritative));
    }

    bool EntityLifecycleReplication::QueueLocalSnapshot(
        const LiveEntityRegistry& liveEntities,
        const std::string& localMap,
        std::uint16_t localMapId,
        std::uint32_t mapEpoch)
    {
        const std::vector<LiveEntityRecord> snapshot = liveEntities.Snapshot();
        for (const LiveEntityRecord& record : snapshot)
        {
            if (!LiveEntityRegistry::IsReplicable(record))
            {
                continue;
            }
            if (localMapId != 0 && record.mapId != 0 &&
                record.mapId != localMapId)
            {
                continue;
            }
            LiveEntityChange change;
            change.kind = LiveEntityChangeKind::Registered;
            change.record = record;
            if (!ObserveLocalChange(
                    change, localMap, localMapId, mapEpoch))
            {
                return false;
            }
        }
        return CompleteLocalMapRoster(localMap, mapEpoch);
    }

    bool EntityLifecycleReplication::CompleteLocalMapRoster(
        const std::string& localMap,
        std::uint32_t mapEpoch)
    {
        if (localMap.empty() || mapEpoch == 0)
        {
            return false;
        }
        if (role_ == PeerRole::Host)
        {
            protocol::EntityLifecycleMessage authoritative;
            bool changed = false;
            if (!directory_.HostCompleteMapRoster(
                    localMap,
                    localActorId_,
                    mapEpoch,
                    authoritative,
                    changed))
            {
                return false;
            }
            if (changed)
            {
                mapSeedAllowances_.erase(localMap);
                char detail[256] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "map=%s owner=%llu epoch=%u role=host-local",
                    localMap.c_str(),
                    static_cast<unsigned long long>(localActorId_),
                    mapEpoch);
                diagnostics_.Event(
                    "MultiplayerMapRosterCompleted",
                    detail);
            }
            return !changed || Queue(std::move(authoritative));
        }

        protocol::EntityLifecycleMessage marker;
        marker.operation = protocol::EntityLifecycleOperation::
            ObserveMapRosterComplete;
        marker.mapEpoch = mapEpoch;
        marker.sourceMapEpoch = mapEpoch;
        marker.mapName = localMap;
        marker.sourceMapName = localMap;
        if (!Queue(std::move(marker)))
        {
            return false;
        }
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "map=%s owner=%llu epoch=%u role=guest-intent",
            localMap.c_str(),
            static_cast<unsigned long long>(localActorId_),
            mapEpoch);
        diagnostics_.Event("MultiplayerMapRosterCompleted", detail);
        return true;
    }

    bool EntityLifecycleReplication::HandleReliableMessage(
        const TransportMessage& transportMessage)
    {
        if (!initialized_ || authority_ == nullptr ||
            transportMessage.type != protocol::PacketType::EntityLifecycle)
        {
            return false;
        }

        protocol::EntityLifecycleMessage message;
        if (!protocol::DecodeEntityLifecycleMessage(
                transportMessage.payload.data(),
                transportMessage.payloadSize,
                message))
        {
            diagnostics_.Event(
                "MultiplayerEntityLifecycleRejected",
                "invalid lifecycle payload");
            return true;
        }

        using protocol::EntityLifecycleOperation;
        const bool intent =
            message.operation == EntityLifecycleOperation::ObservePresent ||
            message.operation == EntityLifecycleOperation::ObserveDormant ||
            message.operation == EntityLifecycleOperation::ObserveTransfer ||
            message.operation == EntityLifecycleOperation::
                ObserveVillageMembershipMutation ||
            message.operation ==
                EntityLifecycleOperation::ObserveMapRosterComplete;
        if (role_ == PeerRole::Host)
        {
            if (!intent || !authority_->IsMapPublisher(
                    message.sourceMapName,
                    transportMessage.sourceActorId,
                    message.sourceMapEpoch))
            {
                diagnostics_.Event(
                    "MultiplayerEntityLifecycleRejected",
                    "guest lifecycle observation had no matching map lease");
                return true;
            }
            protocol::EntityLifecycleMessage authoritative;
            bool changed = false;
            bool accepted = false;
            if (message.operation ==
                EntityLifecycleOperation::ObserveTransfer)
            {
                accepted = HostAcceptTransfer(
                    message,
                    transportMessage.sourceActorId,
                    authoritative,
                    changed);
            }
            else if (message.operation ==
                EntityLifecycleOperation::ObserveMapRosterComplete)
            {
                accepted = HostMayCompleteMapRoster(
                        message,
                        transportMessage.sourceActorId) &&
                    directory_.HostCompleteMapRoster(
                        message.mapName,
                        transportMessage.sourceActorId,
                        message.mapEpoch,
                        authoritative,
                        changed);
                if (accepted)
                {
                    mapSeedAllowances_.erase(message.mapName);
                }
            }
            else if (message.operation == EntityLifecycleOperation::
                    ObserveVillageMembershipMutation)
            {
                accepted = directory_.HostApplyVillageMembershipMutation(
                    message,
                    transportMessage.sourceActorId,
                    authoritative,
                    changed);
            }
            else
            {
                accepted = directory_.HostApplyIntent(
                    message,
                    transportMessage.sourceActorId,
                    authoritative,
                    changed);
            }
            if (!accepted)
            {
                diagnostics_.Event(
                    "MultiplayerEntityLifecycleStale",
                    "guest lifecycle observation was fenced by generation");
                return true;
            }
            if (message.operation ==
                    EntityLifecycleOperation::ObserveMapRosterComplete &&
                changed)
            {
                char detail[256] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "map=%s owner=%llu epoch=%u role=host-accepted",
                    message.mapName.c_str(),
                    static_cast<unsigned long long>(
                        transportMessage.sourceActorId),
                    message.mapEpoch);
                diagnostics_.Event(
                    "MultiplayerMapRosterCompleted",
                    detail);
            }
            if (changed && !Queue(std::move(authoritative)))
            {
                return false;
            }
            return PublishPending();
        }

        if (intent)
        {
            diagnostics_.Event(
                "MultiplayerEntityLifecycleRejected",
                "non-host lifecycle intent was ignored by guest");
            return true;
        }
        const bool applied = directory_.ApplyAuthoritative(message);
        if (!applied)
        {
            diagnostics_.Event(
                "MultiplayerEntityLifecycleStale",
                "host lifecycle update was stale or malformed");
        }
        else if (message.operation ==
            EntityLifecycleOperation::AuthoritativeMapRosterComplete)
        {
            char detail[256] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "map=%s owner=%llu epoch=%u role=guest-applied",
                message.mapName.c_str(),
                static_cast<unsigned long long>(
                    message.simulationOwnerActorId),
                message.mapEpoch);
            diagnostics_.Event("MultiplayerMapRosterCompleted", detail);
        }
        else if (message.operation == EntityLifecycleOperation::
            AuthoritativeMapRosterSeedAllowed)
        {
            char detail[256] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "map=%s owner=%llu epoch=%u role=guest-applied",
                message.mapName.c_str(),
                static_cast<unsigned long long>(
                    message.simulationOwnerActorId),
                message.mapEpoch);
            diagnostics_.Event("MultiplayerMapRosterSeedAllowed", detail);
        }
        return true;
    }

    bool EntityLifecycleReplication::QueueBaseline()
    {
        using protocol::EntityLifecycleOperation;
        const std::vector<WorldEntityRecord> snapshot = directory_.Snapshot();
        const std::vector<MapRosterCompletion> completedMaps =
            directory_.CompletedMapRosters();
        std::vector<protocol::EntityLifecycleMessage> seedAllowances;
        seedAllowances.reserve(mapSeedAllowances_.size());
        for (const auto& entry : mapSeedAllowances_)
        {
            seedAllowances.push_back(entry.second);
        }
        std::sort(
            seedAllowances.begin(),
            seedAllowances.end(),
            [](const protocol::EntityLifecycleMessage& left,
                const protocol::EntityLifecycleMessage& right)
            {
                return left.mapName < right.mapName;
            });
        if (snapshot.size() + completedMaps.size() +
                seedAllowances.size() + 2 >
            PendingMessageCapacity - pending_.size())
        {
            diagnostics_.Event(
                "MultiplayerEntityLifecycleOverflow",
                "host world baseline exceeded the bounded lifecycle queue");
            return false;
        }

        const std::uint32_t baselineId = NextBaselineId();
        protocol::EntityLifecycleMessage begin;
        begin.operation = EntityLifecycleOperation::BaselineBegin;
        begin.worldRevision = directory_.LatestWorldRevision();
        begin.baselineId = baselineId;
        pending_.push_back(std::move(begin));
        for (const WorldEntityRecord& record : snapshot)
        {
            pending_.push_back(WorldEntityDirectory::ToMessage(
                record,
                record.live
                    ? EntityLifecycleOperation::AuthoritativeUpsert
                    : EntityLifecycleOperation::AuthoritativeDormant));
        }
        protocol::EntityLifecycleMessage end;
        end.operation = EntityLifecycleOperation::BaselineEnd;
        end.worldRevision = directory_.LatestWorldRevision();
        end.baselineId = baselineId;
        pending_.push_back(std::move(end));
        for (const MapRosterCompletion& completion : completedMaps)
        {
            protocol::EntityLifecycleMessage marker;
            marker.operation =
                EntityLifecycleOperation::AuthoritativeMapRosterComplete;
            marker.worldRevision = completion.worldRevision;
            marker.simulationOwnerActorId =
                completion.simulationOwnerActorId;
            marker.mapEpoch = completion.mapEpoch;
            marker.mapName = completion.mapName;
            pending_.push_back(std::move(marker));
        }
        for (auto& marker : seedAllowances)
        {
            pending_.push_back(std::move(marker));
        }
        return true;
    }

    bool EntityLifecycleReplication::HostAcceptTransfer(
        const protocol::EntityLifecycleMessage& intent,
        std::uint64_t sourceActorId,
        protocol::EntityLifecycleMessage& authoritative,
        bool& changed)
    {
        if (authority_ == nullptr)
        {
            return false;
        }
        const std::string* const resolvedName =
            authority_->ResolveMapName(intent.mapId);
        const std::string destinationMapName = resolvedName != nullptr
            ? *resolvedName
            : std::string{};
        const authority::MapAuthorityLease* const destinationLease =
            !destinationMapName.empty()
                ? authority_->FindMapLease(destinationMapName)
                : nullptr;
        const std::uint64_t destinationOwnerActorId =
            destinationLease != nullptr ? destinationLease->actorId : 0;
        const std::uint32_t destinationMapEpoch =
            destinationLease != nullptr ? destinationLease->epoch : 0;
        const bool accepted = directory_.HostTransfer(
            intent,
            sourceActorId,
            destinationMapName,
            destinationOwnerActorId,
            destinationMapEpoch,
            authoritative,
            changed);
        if (accepted && changed)
        {
            char detail[448] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "thing_uid=%016llX generation=%u source_owner=%llu source_map=%s source_epoch=%u destination_map=%s destination_map_id=%u destination_owner=%llu destination_epoch=%u",
                static_cast<unsigned long long>(intent.entityUid),
                intent.entityGeneration,
                static_cast<unsigned long long>(sourceActorId),
                intent.sourceMapName.c_str(),
                intent.sourceMapEpoch,
                destinationMapName.c_str(),
                static_cast<unsigned int>(intent.mapId),
                static_cast<unsigned long long>(destinationOwnerActorId),
                destinationMapEpoch);
            diagnostics_.Event("MultiplayerEntityTransferred", detail);
        }
        return accepted;
    }

    bool EntityLifecycleReplication::ResolveUnknownMapNames()
    {
        const std::vector<WorldEntityRecord> snapshot = directory_.Snapshot();
        for (const WorldEntityRecord& record : snapshot)
        {
            if (!record.available || record.live || record.mapId == 0 ||
                !record.mapName.empty())
            {
                continue;
            }
            const std::string* const mapName =
                authority_->ResolveMapName(record.mapId);
            if (mapName == nullptr || mapName->empty())
            {
                continue;
            }
            const authority::MapAuthorityLease* const lease =
                authority_->FindMapLease(*mapName);
            protocol::EntityLifecycleMessage authoritative;
            bool changed = false;
            if (!directory_.HostResolveMapIdentity(
                    record.thingUid,
                    *mapName,
                    lease != nullptr ? lease->actorId : 0,
                    lease != nullptr ? lease->epoch : 0,
                    authoritative,
                    changed))
            {
                return false;
            }
            if (changed && !Queue(std::move(authoritative)))
            {
                return false;
            }
        }
        return true;
    }

    bool EntityLifecycleReplication::HostReconcileMapAuthorities()
    {
        const std::vector<MapRosterCompletion> knownRosters =
            directory_.CompletedMapRosters();
        const std::vector<WorldEntityRecord> snapshot = directory_.Snapshot();
        for (const WorldEntityRecord& record : snapshot)
        {
            if (!record.available || record.mapName.empty())
            {
                continue;
            }
            if (pending_.size() >= PendingMessageCapacity &&
                !PublishPending())
            {
                return false;
            }
            const authority::MapAuthorityLease* const lease =
                authority_->FindMapLease(record.mapName);
            protocol::EntityLifecycleMessage authoritative;
            bool changed = false;
            if (!directory_.HostReconcileMapAuthority(
                    record.thingUid,
                    lease != nullptr ? lease->actorId : 0,
                    lease != nullptr ? lease->epoch : 0,
                    authoritative,
                    changed))
            {
                return false;
            }
            if (changed && !Queue(std::move(authoritative)))
            {
                return false;
            }
        }
        // A completed roster remains canonical when its sticky lease changes.
        // Advance only its boundary to the new epoch after all records have
        // been reconciled. The successor can then apply that host roster before
        // it is permitted to publish native state from its own save.
        for (const MapRosterCompletion& known : knownRosters)
        {
            const authority::MapAuthorityLease* const lease =
                authority_->FindMapLease(known.mapName);
            if (lease == nullptr || lease->actorId == 0 ||
                lease->epoch == 0 ||
                (known.simulationOwnerActorId == lease->actorId &&
                    known.mapEpoch == lease->epoch))
            {
                continue;
            }
            if (pending_.size() >= PendingMessageCapacity &&
                !PublishPending())
            {
                return false;
            }
            protocol::EntityLifecycleMessage authoritative;
            bool changed = false;
            if (!directory_.HostCompleteMapRoster(
                    known.mapName,
                    lease->actorId,
                    lease->epoch,
                    authoritative,
                    changed))
            {
                return false;
            }
            if (changed && !Queue(std::move(authoritative)))
            {
                return false;
            }
        }

        std::unordered_set<std::string> activeUnseededMaps;
        const std::vector<authority::MapAuthorityLease> leases =
            authority_->MapLeases();
        for (const authority::MapAuthorityLease& lease : leases)
        {
            if (lease.mapName.empty() || lease.actorId == 0 ||
                lease.epoch == 0 || directory_.HasMapRoster(lease.mapName))
            {
                mapSeedAllowances_.erase(lease.mapName);
                continue;
            }
            activeUnseededMaps.insert(lease.mapName);
            const auto existing = mapSeedAllowances_.find(lease.mapName);
            if (existing != mapSeedAllowances_.end() &&
                existing->second.simulationOwnerActorId == lease.actorId &&
                existing->second.mapEpoch == lease.epoch)
            {
                continue;
            }
            protocol::EntityLifecycleMessage marker;
            marker.operation = protocol::EntityLifecycleOperation::
                AuthoritativeMapRosterSeedAllowed;
            marker.simulationOwnerActorId = lease.actorId;
            marker.mapEpoch = lease.epoch;
            marker.mapName = lease.mapName;
            if (!Queue(marker))
            {
                return false;
            }
            mapSeedAllowances_[lease.mapName] = std::move(marker);
        }
        for (auto iterator = mapSeedAllowances_.begin();
             iterator != mapSeedAllowances_.end();)
        {
            if (activeUnseededMaps.find(iterator->first) ==
                activeUnseededMaps.end())
            {
                iterator = mapSeedAllowances_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
        return true;
    }

    bool EntityLifecycleReplication::HostMayCompleteMapRoster(
        const protocol::EntityLifecycleMessage& message,
        std::uint64_t sourceActorId) const noexcept
    {
        if (directory_.HasMapRoster(message.mapName))
        {
            return true;
        }
        const auto allowance = mapSeedAllowances_.find(message.mapName);
        return allowance != mapSeedAllowances_.end() &&
            sourceActorId != 0 &&
            allowance->second.simulationOwnerActorId == sourceActorId &&
            allowance->second.mapEpoch == message.mapEpoch;
    }

    bool EntityLifecycleReplication::FlushPendingTransfers()
    {
        for (auto iterator = pendingTransfers_.begin();
             iterator != pendingTransfers_.end();)
        {
            protocol::EntityLifecycleMessage& intent = iterator->second;
            const authority::MapAuthorityLease* const sourceLease =
                authority_->FindMapLease(intent.sourceMapName);
            const WorldEntityRecord* const canonical =
                directory_.Find(intent.entityUid);
            if (sourceLease == nullptr ||
                sourceLease->actorId != localActorId_ ||
                sourceLease->epoch != intent.sourceMapEpoch ||
                (canonical != nullptr &&
                    (!canonical->live || !canonical->available ||
                        canonical->mapName != intent.sourceMapName)))
            {
                iterator = pendingTransfers_.erase(iterator);
                continue;
            }
            if (canonical == nullptr || canonical->generation == 0)
            {
                ++iterator;
                continue;
            }
            intent.entityGeneration = canonical->generation;
            const std::string* const destination =
                authority_->ResolveMapName(intent.mapId);
            intent.mapName = destination != nullptr
                ? *destination
                : std::string{};
            if (!Queue(std::move(intent)))
            {
                return false;
            }
            iterator = pendingTransfers_.erase(iterator);
        }
        return true;
    }

    bool EntityLifecycleReplication::Queue(
        protocol::EntityLifecycleMessage message)
    {
        if (pending_.size() >= PendingMessageCapacity)
        {
            diagnostics_.Event(
                "MultiplayerEntityLifecycleOverflow",
                "bounded lifecycle publication queue is full");
            return false;
        }
        pending_.push_back(std::move(message));
        return true;
    }

    bool EntityLifecycleReplication::PublishPending()
    {
        while (!pending_.empty())
        {
            std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload = {};
            std::size_t payloadSize = 0;
            const protocol::EntityLifecycleMessage& message =
                pending_.front();
            if (!protocol::EncodeEntityLifecycleMessage(
                    message,
                    payload.data(),
                    protocol::MaximumPayloadBytes(),
                    payloadSize))
            {
                char detail[640] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "operation=%u flags=0x%02X uid=%016llX generation=%u revision=%llu owner=%llu map=%s map_id=%u map_epoch=%u source_map=%s source_epoch=%u baseline=%u definition=%u",
                    static_cast<unsigned int>(message.operation),
                    static_cast<unsigned int>(message.flags),
                    static_cast<unsigned long long>(message.entityUid),
                    message.entityGeneration,
                    static_cast<unsigned long long>(message.worldRevision),
                    static_cast<unsigned long long>(
                        message.simulationOwnerActorId),
                    message.mapName.c_str(),
                    static_cast<unsigned int>(message.mapId),
                    message.mapEpoch,
                    message.sourceMapName.c_str(),
                    message.sourceMapEpoch,
                    message.baselineId,
                    static_cast<unsigned int>(message.definitionIndex));
                diagnostics_.Event(
                    "MultiplayerEntityLifecycleEncodeRejected",
                    detail);
                return false;
            }
            if (!transport_->SubmitReliable(
                    protocol::PacketType::EntityLifecycle,
                    payload.data(),
                    payloadSize))
            {
                if (transport_->HasFailed())
                {
                    return false;
                }
                if (!publishBackpressured_)
                {
                    diagnostics_.Event(
                        "MultiplayerEntityLifecyclePublishDeferred",
                        "bounded ordered transport queue is draining; lifecycle state remains queued");
                    publishBackpressured_ = true;
                }
                return true;
            }
            pending_.pop_front();
        }
        if (publishBackpressured_)
        {
            diagnostics_.Event(
                "MultiplayerEntityLifecyclePublishResumed",
                "queued lifecycle state has entered the ordered transport");
            publishBackpressured_ = false;
        }
        return true;
    }

    protocol::EntityLifecycleMessage
        EntityLifecycleReplication::LocalIntent(
            const LiveEntityRecord& record,
            bool present,
            const std::string& localMap,
            std::uint16_t localMapId,
            std::uint32_t mapEpoch) const
    {
        protocol::EntityLifecycleMessage intent;
        const bool transfer = !present && localMapId != 0 &&
            record.mapId != 0 && record.mapId != localMapId;
        intent.operation = transfer
            ? protocol::EntityLifecycleOperation::ObserveTransfer
            : (present
                ? protocol::EntityLifecycleOperation::ObservePresent
                : protocol::EntityLifecycleOperation::ObserveDormant);
        intent.flags = protocol::entity_lifecycle_flag::Available;
        if (record.gamePersistent)
        {
            intent.flags |=
                protocol::entity_lifecycle_flag::GamePersistent;
        }
        if (record.levelPersistent)
        {
            intent.flags |=
                protocol::entity_lifecycle_flag::LevelPersistent;
        }
        if (record.creature)
        {
            intent.flags |= protocol::entity_lifecycle_flag::Creature;
        }
        if (present)
        {
            intent.flags |= protocol::entity_lifecycle_flag::Live;
        }
        if (record.hasTransform)
        {
            intent.flags |= protocol::entity_lifecycle_flag::HasTransform;
            intent.position = record.position;
            intent.facing = record.facing;
        }
        intent.entityUid = record.thingUid;
        if (record.hasVillageMembership)
        {
            intent.flags |= protocol::entity_lifecycle_flag::
                HasVillageMembership;
            intent.villageUid = record.villageUid;
        }
        const WorldEntityRecord* const canonical =
            directory_.Find(record.thingUid);
        intent.entityGeneration = canonical != nullptr
            ? canonical->generation
            : 0;
        intent.sourceMapEpoch = mapEpoch;
        intent.sourceMapName = localMap;
        intent.mapEpoch = transfer ? 0 : mapEpoch;
        intent.mapId = record.mapId;
        intent.definitionIndex = record.definitionIndex;
        intent.scriptName = record.scriptName;
        if (!transfer)
        {
            intent.mapName = localMap;
        }
        else if (authority_ != nullptr)
        {
            const std::string* const destination =
                authority_->ResolveMapName(record.mapId);
            if (destination != nullptr)
            {
                intent.mapName = *destination;
            }
        }
        return intent;
    }

    std::uint32_t EntityLifecycleReplication::NextBaselineId() noexcept
    {
        ++nextBaselineId_;
        if (nextBaselineId_ == 0)
        {
            ++nextBaselineId_;
        }
        return nextBaselineId_;
    }

    const WorldEntityDirectory& EntityLifecycleReplication::Directory()
        const noexcept
    {
        return directory_;
    }

    bool EntityLifecycleReplication::HostAcceptMovement(
        const protocol::EntityMovementMessage& message) noexcept
    {
        return initialized_ && role_ == PeerRole::Host &&
            directory_.HostAcceptMovement(message);
    }

    bool EntityLifecycleReplication::SubmitVillageMembershipMutation(
        std::uint64_t entityUid,
        std::uint64_t villageUid)
    {
        if (!initialized_ || authority_ == nullptr || entityUid == 0)
        {
            return false;
        }
        const WorldEntityRecord* const world = directory_.Find(entityUid);
        if (world == nullptr || !world->live || !world->available ||
            world->generation == 0 || world->mapId == 0 ||
            world->mapEpoch == 0 || world->mapName.empty() ||
            !authority_->IsMapPublisher(
                world->mapName,
                localActorId_,
                world->mapEpoch))
        {
            return true;
        }

        protocol::EntityLifecycleMessage intent;
        intent.operation = protocol::EntityLifecycleOperation::
            ObserveVillageMembershipMutation;
        intent.flags = protocol::entity_lifecycle_flag::Available |
            protocol::entity_lifecycle_flag::Live;
        if (villageUid != 0)
        {
            intent.flags |= protocol::entity_lifecycle_flag::
                HasVillageMembership;
            intent.villageUid = villageUid;
        }
        intent.entityUid = world->thingUid;
        intent.entityGeneration = world->generation;
        intent.mapEpoch = world->mapEpoch;
        intent.sourceMapEpoch = world->mapEpoch;
        intent.mapId = world->mapId;
        intent.mapName = world->mapName;
        intent.sourceMapName = world->mapName;

        if (role_ != PeerRole::Host)
        {
            return Queue(std::move(intent)) && PublishPending();
        }

        protocol::EntityLifecycleMessage authoritative;
        bool changed = false;
        if (!directory_.HostApplyVillageMembershipMutation(
                intent,
                localActorId_,
                authoritative,
                changed))
        {
            return false;
        }
        if (changed && !Queue(std::move(authoritative)))
        {
            return false;
        }
        return PublishPending();
    }

    bool EntityLifecycleReplication::SubmitOwnedTransfer(
        std::uint64_t entityUid,
        std::uint16_t destinationMapId,
        const game::Vector3& destinationPosition,
        float destinationFacing)
    {
        if (!initialized_ || authority_ == nullptr || entityUid == 0 ||
            destinationMapId == 0 ||
            !std::isfinite(destinationPosition.x) ||
            !std::isfinite(destinationPosition.y) ||
            !std::isfinite(destinationPosition.z) ||
            !std::isfinite(destinationFacing))
        {
            return false;
        }
        const WorldEntityRecord* const world = directory_.Find(entityUid);
        if (world == nullptr || !world->live || !world->available ||
            world->generation == 0 || world->mapId == 0 ||
            world->mapId == destinationMapId || world->mapName.empty() ||
            world->mapEpoch == 0 || !authority_->IsMapPublisher(
                world->mapName,
                localActorId_,
                world->mapEpoch))
        {
            return false;
        }

        protocol::EntityLifecycleMessage intent =
            WorldEntityDirectory::ToMessage(
                *world,
                protocol::EntityLifecycleOperation::ObserveTransfer);
        intent.sourceMapName = world->mapName;
        intent.sourceMapEpoch = world->mapEpoch;
        intent.mapId = destinationMapId;
        intent.flags |= protocol::entity_lifecycle_flag::HasTransform;
        intent.position = destinationPosition;
        intent.facing = destinationFacing;
        intent.mapEpoch = 0;
        intent.simulationOwnerActorId = 0;
        const std::string* const destination =
            authority_->ResolveMapName(destinationMapId);
        intent.mapName = destination != nullptr
            ? *destination
            : std::string{};

        if (role_ != PeerRole::Host)
        {
            return Queue(std::move(intent)) && PublishPending();
        }

        protocol::EntityLifecycleMessage authoritative;
        bool changed = false;
        if (!HostAcceptTransfer(
                intent,
                localActorId_,
                authoritative,
                changed))
        {
            return false;
        }
        if (changed && !Queue(std::move(authoritative)))
        {
            return false;
        }
        return PublishPending();
    }

    void EntityLifecycleReplication::Shutdown() noexcept
    {
        directory_.Clear();
        pending_.clear();
        pendingTransfers_.clear();
        mapSeedAllowances_.clear();
        transport_ = nullptr;
        authority_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        knownPeerRevision_ = 0;
        nextBaselineId_ = 0;
        lastOwnedMapEpoch_ = 0;
        lastOwnedMap_.clear();
        lastOwnedRosterReady_ = false;
        publishBackpressured_ = false;
        initialized_ = false;
    }
}
