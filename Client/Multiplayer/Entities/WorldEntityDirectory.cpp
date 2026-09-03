#include "WorldEntityDirectory.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fable::multiplayer::entities
{
    bool WorldEntityDirectory::HostObserve(
        const LiveEntityChange& change,
        const std::string& mapName,
        std::uint64_t simulationOwnerActorId,
        std::uint32_t mapEpoch,
        protocol::EntityLifecycleMessage& authoritative,
        bool& changed)
    {
        authoritative = {};
        changed = false;
        if (!LiveEntityRegistry::IsReplicable(change.record) ||
            simulationOwnerActorId == 0 || mapEpoch == 0 || mapName.empty())
        {
            return false;
        }

        WorldEntityRecord observed;
        observed.thingUid = change.record.thingUid;
        observed.villageUid = change.record.villageUid;
        observed.simulationOwnerActorId = simulationOwnerActorId;
        observed.mapEpoch = mapEpoch;
        observed.localIncarnation = change.record.localIncarnation;
        observed.mapId = change.record.mapId;
        observed.definitionIndex = change.record.definitionIndex;
        observed.position = change.record.position;
        observed.facing = change.record.facing;
        observed.hasTransform = change.record.hasTransform;
        observed.gamePersistent = change.record.gamePersistent;
        observed.levelPersistent = change.record.levelPersistent;
        observed.creature = change.record.creature;
        observed.hasVillageMembership =
            change.record.hasVillageMembership;
        observed.live = change.kind != LiveEntityChangeKind::Unregistered;
        observed.available = true;
        observed.mapName = mapName;
        observed.scriptName = change.record.scriptName;
        return HostApplyCurrent(
            observed,
            observed.live,
            authoritative,
            changed);
    }

    bool WorldEntityDirectory::HostApplyIntent(
        const protocol::EntityLifecycleMessage& intent,
        std::uint64_t simulationOwnerActorId,
        protocol::EntityLifecycleMessage& authoritative,
        bool& changed)
    {
        using protocol::EntityLifecycleOperation;
        authoritative = {};
        changed = false;
        if ((intent.operation != EntityLifecycleOperation::ObservePresent &&
                intent.operation != EntityLifecycleOperation::ObserveDormant) ||
            intent.entityUid == 0 || intent.mapEpoch == 0 ||
            intent.sourceMapEpoch != intent.mapEpoch ||
            intent.sourceMapName != intent.mapName ||
            intent.mapName.empty() || simulationOwnerActorId == 0)
        {
            return false;
        }

        const auto existing = records_.find(intent.entityUid);
        if (intent.entityGeneration != 0 &&
            (existing == records_.end() ||
                existing->second.generation != intent.entityGeneration))
        {
            return false;
        }
        // The caller has already been checked against the current sticky map
        // lease. Permit its new epoch to take over records in the same map,
        // but never let a late source teardown pull a transferred entity back.
        if (existing != records_.end() &&
            existing->second.mapName != intent.sourceMapName)
        {
            return false;
        }

        WorldEntityRecord observed;
        observed.thingUid = intent.entityUid;
        observed.villageUid = intent.villageUid;
        observed.simulationOwnerActorId = simulationOwnerActorId;
        observed.mapEpoch = intent.sourceMapEpoch;
        observed.mapId = intent.mapId;
        observed.definitionIndex = intent.definitionIndex;
        observed.position = intent.position;
        observed.facing = intent.facing;
        observed.hasTransform =
            (intent.flags &
                protocol::entity_lifecycle_flag::HasTransform) != 0;
        observed.gamePersistent =
            (intent.flags &
                protocol::entity_lifecycle_flag::GamePersistent) != 0;
        observed.levelPersistent =
            (intent.flags &
                protocol::entity_lifecycle_flag::LevelPersistent) != 0;
        observed.creature =
            (intent.flags & protocol::entity_lifecycle_flag::Creature) != 0;
        observed.hasVillageMembership =
            (intent.flags & protocol::entity_lifecycle_flag::
                HasVillageMembership) != 0;
        observed.live = intent.operation ==
            EntityLifecycleOperation::ObservePresent;
        observed.available = true;
        observed.mapName = intent.mapName;
        observed.definitionName = intent.definitionName;
        observed.scriptName = intent.scriptName;
        return HostApplyCurrent(
            observed,
            observed.live,
            authoritative,
            changed);
    }

    bool WorldEntityDirectory::HostApplyVillageMembershipMutation(
        const protocol::EntityLifecycleMessage& intent,
        std::uint64_t simulationOwnerActorId,
        protocol::EntityLifecycleMessage& authoritative,
        bool& changed)
    {
        authoritative = {};
        changed = false;
        if (intent.operation != protocol::EntityLifecycleOperation::
                ObserveVillageMembershipMutation ||
            intent.entityUid == 0 || intent.entityGeneration == 0 ||
            intent.mapId == 0 || intent.mapEpoch == 0 ||
            intent.sourceMapEpoch != intent.mapEpoch ||
            intent.sourceMapName != intent.mapName ||
            intent.mapName.empty() || simulationOwnerActorId == 0)
        {
            return false;
        }

        const auto existing = records_.find(intent.entityUid);
        if (existing == records_.end())
        {
            return false;
        }
        WorldEntityRecord& current = existing->second;
        if (current.generation != intent.entityGeneration ||
            current.mapId != intent.mapId ||
            current.mapName != intent.mapName ||
            current.mapEpoch != intent.mapEpoch ||
            current.simulationOwnerActorId != simulationOwnerActorId ||
            !current.live || !current.available)
        {
            return false;
        }

        const bool hasVillageMembership =
            (intent.flags & protocol::entity_lifecycle_flag::
                HasVillageMembership) != 0;
        const std::uint64_t villageUid = hasVillageMembership
            ? intent.villageUid
            : 0;
        if (current.hasVillageMembership == hasVillageMembership &&
            current.villageUid == villageUid)
        {
            return true;
        }

        current.hasVillageMembership = hasVillageMembership;
        current.villageUid = villageUid;
        current.worldRevision = NextWorldRevision();
        authoritative = ToMessage(
            current,
            protocol::EntityLifecycleOperation::AuthoritativeUpsert);
        changed = true;
        return true;
    }

    bool WorldEntityDirectory::HostTransfer(
        const protocol::EntityLifecycleMessage& intent,
        std::uint64_t sourceActorId,
        const std::string& destinationMapName,
        std::uint64_t destinationOwnerActorId,
        std::uint32_t destinationMapEpoch,
        protocol::EntityLifecycleMessage& authoritative,
        bool& changed)
    {
        authoritative = {};
        changed = false;
        const bool destinationLeaseSane =
            (destinationOwnerActorId == 0 && destinationMapEpoch == 0) ||
            (destinationOwnerActorId != 0 && destinationMapEpoch != 0 &&
                !destinationMapName.empty());
        if (intent.operation !=
                protocol::EntityLifecycleOperation::ObserveTransfer ||
            intent.entityUid == 0 || intent.entityGeneration == 0 ||
            intent.mapId == 0 || intent.sourceMapName.empty() ||
            intent.sourceMapEpoch == 0 || sourceActorId == 0 ||
            !destinationLeaseSane)
        {
            return false;
        }

        const auto existing = records_.find(intent.entityUid);
        if (existing == records_.end())
        {
            return false;
        }
        WorldEntityRecord& current = existing->second;
        if (current.generation != intent.entityGeneration ||
            current.mapId == intent.mapId ||
            current.mapName != intent.sourceMapName ||
            current.mapEpoch != intent.sourceMapEpoch ||
            current.simulationOwnerActorId != sourceActorId ||
            !current.live || !current.available)
        {
            return false;
        }

        if (intent.lowSimulationRevision != 0 &&
            current.hasLowSimulation &&
            (intent.lowSimulationRevision < current.lowSimulationRevision ||
                (intent.lowSimulationRevision ==
                    current.lowSimulationRevision &&
                    intent.lowSimulation != current.lowSimulation)))
        {
            return false;
        }

        current.mapId = intent.mapId;
        current.mapName = destinationMapName;
        current.simulationOwnerActorId = destinationOwnerActorId;
        current.mapEpoch = destinationMapEpoch;
        if (intent.lowSimulationRevision != 0 &&
            (!current.hasLowSimulation ||
                intent.lowSimulationRevision > current.lowSimulationRevision))
        {
            game::npc::simulation::DummyVillagerState merged =
                intent.lowSimulation;
            if (current.hasLowSimulation)
            {
                merged.creatureUid = current.lowSimulation.creatureUid;
            }
            current.hasLowSimulation = true;
            current.lowSimulation = merged;
            current.lowSimulationRevision = intent.lowSimulationRevision;
        }
        if ((intent.flags &
                protocol::entity_lifecycle_flag::HasTransform) != 0)
        {
            current.position = intent.position;
            current.facing = intent.facing;
            current.hasTransform = true;
        }
        current.live = false;
        current.available = true;
        current.awaitingMaterialization = true;
        current.worldRevision = NextWorldRevision();
        authoritative = ToMessage(
            current,
            protocol::EntityLifecycleOperation::AuthoritativeDormant);
        changed = true;
        return true;
    }

    bool WorldEntityDirectory::HostResolveMapIdentity(
        std::uint64_t thingUid,
        const std::string& mapName,
        std::uint64_t simulationOwnerActorId,
        std::uint32_t mapEpoch,
        protocol::EntityLifecycleMessage& authoritative,
        bool& changed)
    {
        authoritative = {};
        changed = false;
        const bool leaseSane =
            (simulationOwnerActorId == 0 && mapEpoch == 0) ||
            (simulationOwnerActorId != 0 && mapEpoch != 0);
        const auto existing = records_.find(thingUid);
        if (thingUid == 0 || mapName.empty() || !leaseSane ||
            existing == records_.end())
        {
            return false;
        }
        WorldEntityRecord& current = existing->second;
        if (!current.available || current.live || current.mapId == 0 ||
            !current.mapName.empty())
        {
            return true;
        }
        current.mapName = mapName;
        current.simulationOwnerActorId = simulationOwnerActorId;
        current.mapEpoch = mapEpoch;
        current.worldRevision = NextWorldRevision();
        authoritative = ToMessage(
            current,
            protocol::EntityLifecycleOperation::AuthoritativeDormant);
        changed = true;
        return true;
    }

    bool WorldEntityDirectory::HostReconcileMapAuthority(
        std::uint64_t thingUid,
        std::uint64_t simulationOwnerActorId,
        std::uint32_t mapEpoch,
        protocol::EntityLifecycleMessage& authoritative,
        bool& changed)
    {
        authoritative = {};
        changed = false;
        const bool leaseSane =
            (simulationOwnerActorId == 0 && mapEpoch == 0) ||
            (simulationOwnerActorId != 0 && mapEpoch != 0);
        const auto existing = records_.find(thingUid);
        if (thingUid == 0 || !leaseSane || existing == records_.end() ||
            !existing->second.available || existing->second.mapName.empty())
        {
            return false;
        }

        WorldEntityRecord& current = existing->second;
        const bool mapBecameDormant = simulationOwnerActorId == 0 &&
            current.live;
        const bool authorityChanged =
            current.simulationOwnerActorId != simulationOwnerActorId ||
            current.mapEpoch != mapEpoch;
        const bool mapActivated = current.simulationOwnerActorId == 0 &&
            simulationOwnerActorId != 0;
        const bool materializationRequested = mapActivated && !current.live &&
            !current.awaitingMaterialization;
        if (!mapBecameDormant && !authorityChanged &&
            !materializationRequested)
        {
            return true;
        }

        current.simulationOwnerActorId = simulationOwnerActorId;
        current.mapEpoch = mapEpoch;
        if (mapBecameDormant)
        {
            current.live = false;
        }
        if (materializationRequested)
        {
            // The host's dormant world/save projection is canonical. The
            // first occupant of an empty map must reconstruct that roster even
            // when their local hero save would not have spawned the NPC.
            current.awaitingMaterialization = true;
        }
        current.worldRevision = NextWorldRevision();

        if (simulationOwnerActorId == 0 && !current.live &&
            !current.gamePersistent &&
            !current.levelPersistent)
        {
            WorldEntityRecord retired = current;
            retired.available = false;
            retired.awaitingMaterialization = false;
            authoritative = ToMessage(
                retired,
                protocol::EntityLifecycleOperation::AuthoritativeRetire);
            records_.erase(existing);
        }
        else
        {
            authoritative = ToMessage(
                current,
                current.live
                    ? protocol::EntityLifecycleOperation::AuthoritativeUpsert
                    : protocol::EntityLifecycleOperation::AuthoritativeDormant);
        }
        changed = true;
        return true;
    }

    bool WorldEntityDirectory::HostCompleteMapRoster(
        const std::string& mapName,
        std::uint16_t mapId,
        std::uint64_t simulationOwnerActorId,
        std::uint32_t mapEpoch,
        protocol::EntityLifecycleMessage& authoritative,
        bool& changed)
    {
        authoritative = {};
        changed = false;
        if (mapName.empty() || mapId == 0 ||
            simulationOwnerActorId == 0 || mapEpoch == 0)
        {
            return false;
        }

        const auto existing = completedMapRosters_.find(mapId);
        if (existing != completedMapRosters_.end() &&
            existing->second.simulationOwnerActorId ==
                simulationOwnerActorId &&
            existing->second.mapEpoch == mapEpoch &&
            existing->second.mapName == mapName)
        {
            return true;
        }

        MapRosterCompletion completion;
        completion.simulationOwnerActorId = simulationOwnerActorId;
        completion.worldRevision = NextWorldRevision();
        completion.mapEpoch = mapEpoch;
        completion.mapId = mapId;
        completion.mapName = mapName;
        completedMapRosters_[mapId] = completion;

        authoritative.operation = protocol::EntityLifecycleOperation::
            AuthoritativeMapRosterComplete;
        authoritative.worldRevision = completion.worldRevision;
        authoritative.simulationOwnerActorId = simulationOwnerActorId;
        authoritative.mapEpoch = mapEpoch;
        authoritative.mapId = mapId;
        authoritative.mapName = mapName;
        changed = true;
        return true;
    }

    bool WorldEntityDirectory::HostApplyCurrent(
        const WorldEntityRecord& observed,
        bool present,
        protocol::EntityLifecycleMessage& authoritative,
        bool& changed)
    {
        using protocol::EntityLifecycleOperation;
        changed = false;
        auto existing = records_.find(observed.thingUid);
        if (existing == records_.end())
        {
            if (!present && !observed.gamePersistent &&
                !observed.levelPersistent)
            {
                return true;
            }
            if (records_.size() >= MaximumRecords)
            {
                return false;
            }
            WorldEntityRecord created = observed;
            // Once a canonical roster has been completed, a newly observed
            // live creature is a runtime birth rather than a member expected
            // from retail saved-map construction. Every same-map observer must
            // reconstruct it just like an explicit cross-map arrival.
            created.awaitingMaterialization = present && created.creature &&
                HasMapRoster(created.mapId);
            created.generation = NextGeneration();
            created.worldRevision = NextWorldRevision();
            records_.emplace(created.thingUid, created);
            authoritative = ToMessage(
                created,
                present
                    ? EntityLifecycleOperation::AuthoritativeUpsert
                    : EntityLifecycleOperation::AuthoritativeDormant);
            changed = true;
            return true;
        }

        WorldEntityRecord& current = existing->second;
        if (!present && !current.gamePersistent &&
            !current.levelPersistent && !observed.gamePersistent &&
            !observed.levelPersistent)
        {
            WorldEntityRecord retired = current;
            retired.live = false;
            retired.available = false;
            retired.awaitingMaterialization = false;
            retired.mapEpoch = observed.mapEpoch;
            retired.simulationOwnerActorId =
                observed.simulationOwnerActorId;
            retired.worldRevision = NextWorldRevision();
            authoritative = ToMessage(
                retired,
                EntityLifecycleOperation::AuthoritativeRetire);
            records_.erase(existing);
            changed = true;
            return true;
        }

        const bool meaningful = current.mapId != observed.mapId ||
            current.definitionIndex != observed.definitionIndex ||
            current.mapName != observed.mapName ||
            current.gamePersistent != observed.gamePersistent ||
            current.levelPersistent != observed.levelPersistent ||
            current.creature != observed.creature ||
            (observed.hasTransform &&
                (current.position.x != observed.position.x ||
                    current.position.y != observed.position.y ||
                    current.position.z != observed.position.z ||
                    current.facing != observed.facing)) ||
            current.live != present || !current.available ||
            current.awaitingMaterialization ||
            current.simulationOwnerActorId !=
                observed.simulationOwnerActorId ||
            current.mapEpoch != observed.mapEpoch ||
            (!observed.definitionName.empty() &&
                current.definitionName != observed.definitionName) ||
            (!observed.scriptName.empty() &&
                current.scriptName != observed.scriptName);
        current.localIncarnation = observed.localIncarnation;
        if (!meaningful)
        {
            return true;
        }

        current.mapId = observed.mapId;
        current.definitionIndex = observed.definitionIndex;
        current.mapName = observed.mapName;
        current.gamePersistent = observed.gamePersistent;
        current.levelPersistent = observed.levelPersistent;
        current.creature = observed.creature;
        // Existing VillageUID state is host-save truth. A new map owner may
        // have loaded a stale local save, so roster observations cannot
        // overwrite it. Legitimate changes use a dedicated typed mutation.
        if (observed.hasTransform)
        {
            current.position = observed.position;
            current.facing = observed.facing;
            current.hasTransform = true;
        }
        const bool retainArrivalMaterialization =
            present && current.awaitingMaterialization;
        current.live = present;
        current.available = true;
        current.awaitingMaterialization = retainArrivalMaterialization;
        current.simulationOwnerActorId = observed.simulationOwnerActorId;
        current.mapEpoch = observed.mapEpoch;
        if (!observed.definitionName.empty())
        {
            current.definitionName = observed.definitionName;
        }
        if (!observed.scriptName.empty())
        {
            current.scriptName = observed.scriptName;
        }
        current.worldRevision = NextWorldRevision();
        authoritative = ToMessage(
            current,
            present
                ? EntityLifecycleOperation::AuthoritativeUpsert
                : EntityLifecycleOperation::AuthoritativeDormant);
        changed = true;
        return true;
    }

    bool WorldEntityDirectory::ApplyAuthoritative(
        const protocol::EntityLifecycleMessage& message)
    {
        using protocol::EntityLifecycleOperation;
        if (message.operation == EntityLifecycleOperation::BaselineBegin ||
            message.operation == EntityLifecycleOperation::BaselineEnd)
        {
            return ApplyBaselineBoundary(message);
        }
        if (message.operation ==
            EntityLifecycleOperation::AuthoritativeMapRosterSeedAllowed)
        {
            if (completedMapRosters_.find(message.mapId) !=
                completedMapRosters_.end())
            {
                return false;
            }
            const auto existing = mapSeedPermissions_.find(message.mapId);
            if (existing != mapSeedPermissions_.end() &&
                (existing->second.mapEpoch > message.mapEpoch ||
                    (existing->second.mapEpoch == message.mapEpoch &&
                        existing->second.simulationOwnerActorId !=
                            message.simulationOwnerActorId)))
            {
                return false;
            }
            MapRosterSeedPermission permission;
            permission.simulationOwnerActorId =
                message.simulationOwnerActorId;
            permission.mapEpoch = message.mapEpoch;
            mapSeedPermissions_[message.mapId] = permission;
            return true;
        }
        if (message.operation ==
            EntityLifecycleOperation::AuthoritativeMapRosterComplete)
        {
            const auto existing = completedMapRosters_.find(message.mapId);
            if (existing != completedMapRosters_.end() &&
                (existing->second.mapEpoch > message.mapEpoch ||
                    (existing->second.mapEpoch == message.mapEpoch &&
                        (existing->second.simulationOwnerActorId !=
                                message.simulationOwnerActorId ||
                            existing->second.worldRevision >=
                                message.worldRevision))))
            {
                return false;
            }
            MapRosterCompletion completion;
            completion.simulationOwnerActorId =
                message.simulationOwnerActorId;
            completion.worldRevision = message.worldRevision;
            completion.mapEpoch = message.mapEpoch;
            completion.mapId = message.mapId;
            completion.mapName = message.mapName;
            completedMapRosters_[message.mapId] =
                std::move(completion);
            mapSeedPermissions_.erase(message.mapId);
            nextWorldRevision_ = (std::max)(
                nextWorldRevision_, message.worldRevision);
            return true;
        }
        if (message.operation !=
                EntityLifecycleOperation::AuthoritativeUpsert &&
            message.operation !=
                EntityLifecycleOperation::AuthoritativeDormant &&
            message.operation !=
                EntityLifecycleOperation::AuthoritativeRetire)
        {
            return false;
        }

        auto existing = records_.find(message.entityUid);
        if (existing != records_.end() &&
            existing->second.worldRevision > message.worldRevision)
        {
            return false;
        }
        if (existing != records_.end() &&
            existing->second.worldRevision == message.worldRevision)
        {
            if (existing->second.generation != message.entityGeneration)
            {
                return false;
            }
            if (activeBaselineId_ != 0)
            {
                existing->second.position = message.position;
                existing->second.facing = message.facing;
                existing->second.hasTransform =
                    (message.flags &
                        protocol::entity_lifecycle_flag::HasTransform) != 0;
                existing->second.villageUid = message.villageUid;
                existing->second.hasVillageMembership =
                    (message.flags & protocol::entity_lifecycle_flag::
                        HasVillageMembership) != 0;
            }
            existing->second.seenBaselineId = activeBaselineId_;
            return true;
        }
        if (message.operation ==
            EntityLifecycleOperation::AuthoritativeRetire)
        {
            if (existing != records_.end() &&
                existing->second.generation == message.entityGeneration)
            {
                records_.erase(existing);
            }
            nextWorldRevision_ = (std::max)(
                nextWorldRevision_, message.worldRevision);
            return true;
        }

        if (existing == records_.end() && records_.size() >= MaximumRecords)
        {
            return false;
        }

        // Ordinary lifecycle messages can omit the separately replicated
        // CTCDummyVillager row, while handoff snapshots carry it explicitly.
        // Preserve the typed overlay only when the incoming structural record
        // has no newer row for this host-issued incarnation.
        const bool preserveLowSimulation = existing != records_.end() &&
            existing->second.generation == message.entityGeneration &&
            existing->second.hasLowSimulation;
        WorldEntityRecord record;
        record.thingUid = message.entityUid;
        record.villageUid = message.villageUid;
        record.generation = message.entityGeneration;
        record.worldRevision = message.worldRevision;
        record.simulationOwnerActorId = message.simulationOwnerActorId;
        record.mapEpoch = message.mapEpoch;
        record.mapId = message.mapId;
        record.definitionIndex = message.definitionIndex;
        record.position = message.position;
        record.facing = message.facing;
        record.hasTransform =
            (message.flags &
                protocol::entity_lifecycle_flag::HasTransform) != 0;
        record.gamePersistent =
            (message.flags &
                protocol::entity_lifecycle_flag::GamePersistent) != 0;
        record.levelPersistent =
            (message.flags &
                protocol::entity_lifecycle_flag::LevelPersistent) != 0;
        record.creature =
            (message.flags & protocol::entity_lifecycle_flag::Creature) != 0;
        record.live =
            (message.flags & protocol::entity_lifecycle_flag::Live) != 0;
        record.available =
            (message.flags & protocol::entity_lifecycle_flag::Available) != 0;
        record.awaitingMaterialization =
            (message.flags & protocol::entity_lifecycle_flag::
                AwaitingMaterialization) != 0;
        record.hasVillageMembership =
            (message.flags & protocol::entity_lifecycle_flag::
                HasVillageMembership) != 0;
        if (message.lowSimulationRevision != 0 &&
            (!preserveLowSimulation ||
                message.lowSimulationRevision >=
                    existing->second.lowSimulationRevision))
        {
            record.hasLowSimulation = true;
            record.lowSimulation = message.lowSimulation;
            record.lowSimulationRevision = message.lowSimulationRevision;
        }
        if (preserveLowSimulation &&
            (message.lowSimulationRevision == 0 ||
                existing->second.lowSimulationRevision >=
                    message.lowSimulationRevision))
        {
            record.hasLowSimulation = existing->second.hasLowSimulation;
            record.lowSimulation = existing->second.lowSimulation;
            record.lowSimulationRevision =
                existing->second.lowSimulationRevision;
        }
        record.mapName = message.mapName;
        record.definitionName = message.definitionName;
        record.scriptName = message.scriptName;
        record.seenBaselineId = activeBaselineId_;
        records_[record.thingUid] = std::move(record);
        nextGeneration_ = (std::max)(
            nextGeneration_, message.entityGeneration);
        nextWorldRevision_ = (std::max)(
            nextWorldRevision_, message.worldRevision);
        return true;
    }

    bool WorldEntityDirectory::HostAcceptMovement(
        const protocol::EntityMovementMessage& message) noexcept
    {
        const auto existing = records_.find(message.entityUid);
        if (existing == records_.end() ||
            existing->second.generation != message.entityGeneration ||
            existing->second.mapEpoch != message.mapEpoch ||
            existing->second.mapName != message.mapName ||
            !existing->second.live || !existing->second.available ||
            !std::isfinite(message.position.x) ||
            !std::isfinite(message.position.y) ||
            !std::isfinite(message.position.z) ||
            !std::isfinite(message.facing) || message.facing < 0.0f ||
            message.facing >= 1.0f)
        {
            return false;
        }
        WorldEntityRecord& current = existing->second;
        const bool changed = !current.hasTransform ||
            current.position.x != message.position.x ||
            current.position.y != message.position.y ||
            current.position.z != message.position.z ||
            current.facing != message.facing;
        current.position = message.position;
        current.facing = message.facing;
        current.hasTransform = true;
        if (changed)
        {
            // Transform-only movement is still a canonical world mutation.
            // Advance the same revision consumed by save projection and future
            // lifecycle baselines.
            current.worldRevision = NextWorldRevision();
        }
        return true;
    }

    bool WorldEntityDirectory::HostApplyLowSimulation(
        std::uint64_t thingUid,
        std::uint32_t generation,
        const std::string& mapName,
        std::uint64_t simulationOwnerActorId,
        std::uint32_t mapEpoch,
        const game::npc::simulation::DummyVillagerState& state,
        std::uint32_t revision,
        bool& changed) noexcept
    {
        changed = false;
        const auto existing = records_.find(thingUid);
        if (thingUid == 0 || generation == 0 || mapName.empty() ||
            simulationOwnerActorId == 0 || mapEpoch == 0 || revision == 0 ||
            !state.componentPresent || existing == records_.end())
        {
            return false;
        }

        WorldEntityRecord& current = existing->second;
        if (!current.available || !current.live ||
            current.generation != generation || current.mapName != mapName ||
            current.simulationOwnerActorId != simulationOwnerActorId ||
            current.mapEpoch != mapEpoch)
        {
            return false;
        }

        // The wire message intentionally omits the native CreatureUID. Keep
        // the identity learned from the local component while merging the
        // mutable schedule fields from the map owner.
        game::npc::simulation::DummyVillagerState merged = state;
        if (current.hasLowSimulation)
        {
            merged.creatureUid = current.lowSimulation.creatureUid;
        }
        if (current.hasLowSimulation && revision <
                current.lowSimulationRevision)
        {
            return false;
        }
        if (current.hasLowSimulation && revision ==
                current.lowSimulationRevision)
        {
            return current.lowSimulation == merged;
        }

        current.hasLowSimulation = true;
        current.lowSimulation = merged;
        current.lowSimulationRevision = revision;
        current.worldRevision = NextWorldRevision();
        changed = true;
        return true;
    }

    bool WorldEntityDirectory::ApplyBaselineBoundary(
        const protocol::EntityLifecycleMessage& message)
    {
        using protocol::EntityLifecycleOperation;
        if (message.operation == EntityLifecycleOperation::BaselineBegin)
        {
            if (activeBaselineId_ != 0)
            {
                return false;
            }
            activeBaselineId_ = message.baselineId;
            activeBaselineRevision_ = message.worldRevision;
            authoritativeBaselineReady_ = false;
            // A completion marker is meaningful only relative to the exact
            // canonical roster that preceded it on the reliable stream. Do
            // not let a marker retained from an earlier baseline authorize
            // replica removals while this replacement roster is incomplete.
            // QueueBaseline emits fresh markers after BaselineEnd.
            completedMapRosters_.clear();
            mapSeedPermissions_.clear();
            return true;
        }
        if (message.operation != EntityLifecycleOperation::BaselineEnd ||
            activeBaselineId_ == 0 ||
            message.baselineId != activeBaselineId_ ||
            message.worldRevision != activeBaselineRevision_)
        {
            return false;
        }
        for (auto iterator = records_.begin(); iterator != records_.end();)
        {
            if (iterator->second.seenBaselineId != activeBaselineId_)
            {
                iterator = records_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
        nextWorldRevision_ = (std::max)(
            nextWorldRevision_, activeBaselineRevision_);
        activeBaselineId_ = 0;
        activeBaselineRevision_ = 0;
        authoritativeBaselineReady_ = true;
        return true;
    }

    const WorldEntityRecord* WorldEntityDirectory::Find(
        std::uint64_t thingUid) const noexcept
    {
        const auto match = records_.find(thingUid);
        return match != records_.end() ? &match->second : nullptr;
    }

    const WorldEntityRecord*
        WorldEntityDirectory::FindUniqueByScriptIdentity(
            const char* scriptName,
            std::uint16_t definitionIndex) const noexcept
    {
        if (scriptName == nullptr || scriptName[0] == '\0')
        {
            return nullptr;
        }

        const WorldEntityRecord* match = nullptr;
        for (const auto& entry : records_)
        {
            const WorldEntityRecord& candidate = entry.second;
            if (!candidate.available ||
                candidate.definitionIndex != definitionIndex ||
                candidate.scriptName != scriptName)
            {
                continue;
            }
            if (match != nullptr && match->thingUid != candidate.thingUid)
            {
                return nullptr;
            }
            match = &candidate;
        }
        return match;
    }

    std::vector<WorldEntityRecord> WorldEntityDirectory::Snapshot() const
    {
        std::vector<WorldEntityRecord> result;
        result.reserve(records_.size());
        for (const auto& entry : records_)
        {
            result.push_back(entry.second);
        }
        std::sort(
            result.begin(),
            result.end(),
            [](const WorldEntityRecord& left, const WorldEntityRecord& right)
            {
                return left.thingUid < right.thingUid;
            });
        return result;
    }

    std::vector<MapRosterCompletion>
        WorldEntityDirectory::CompletedMapRosters() const
    {
        std::vector<MapRosterCompletion> result;
        result.reserve(completedMapRosters_.size());
        for (const auto& entry : completedMapRosters_)
        {
            result.push_back(entry.second);
        }
        std::sort(
            result.begin(),
            result.end(),
            [](const MapRosterCompletion& left,
                const MapRosterCompletion& right)
            {
                return left.mapId < right.mapId;
            });
        return result;
    }

    bool WorldEntityDirectory::IsMapRosterComplete(
        std::uint16_t mapId,
        std::uint32_t mapEpoch) const noexcept
    {
        const auto completion = completedMapRosters_.find(mapId);
        return completion != completedMapRosters_.end() && mapEpoch != 0 &&
            completion->second.mapEpoch == mapEpoch;
    }

    bool WorldEntityDirectory::HasMapRoster(
        std::uint16_t mapId) const noexcept
    {
        return mapId != 0 && completedMapRosters_.find(mapId) !=
                completedMapRosters_.end();
    }

    bool WorldEntityDirectory::IsMapSeedAllowed(
        std::uint16_t mapId,
        std::uint64_t simulationOwnerActorId,
        std::uint32_t mapEpoch) const noexcept
    {
        const auto permission = mapSeedPermissions_.find(mapId);
        return permission != mapSeedPermissions_.end() &&
            simulationOwnerActorId != 0 && mapEpoch != 0 &&
            permission->second.simulationOwnerActorId ==
                simulationOwnerActorId &&
            permission->second.mapEpoch == mapEpoch;
    }

    bool WorldEntityDirectory::HasAuthoritativeBaseline() const noexcept
    {
        return authoritativeBaselineReady_;
    }

    std::uint64_t WorldEntityDirectory::LatestWorldRevision() const noexcept
    {
        return nextWorldRevision_;
    }

    std::size_t WorldEntityDirectory::Size() const noexcept
    {
        return records_.size();
    }

    void WorldEntityDirectory::Clear() noexcept
    {
        records_.clear();
        completedMapRosters_.clear();
        mapSeedPermissions_.clear();
        nextGeneration_ = 0;
        nextWorldRevision_ = 0;
        activeBaselineId_ = 0;
        activeBaselineRevision_ = 0;
        authoritativeBaselineReady_ = false;
    }

    std::uint32_t WorldEntityDirectory::NextGeneration() noexcept
    {
        ++nextGeneration_;
        if (nextGeneration_ == 0)
        {
            ++nextGeneration_;
        }
        return nextGeneration_;
    }

    std::uint64_t WorldEntityDirectory::NextWorldRevision() noexcept
    {
        ++nextWorldRevision_;
        if (nextWorldRevision_ == 0)
        {
            ++nextWorldRevision_;
        }
        return nextWorldRevision_;
    }

    std::uint8_t WorldEntityDirectory::Flags(
        const WorldEntityRecord& record) noexcept
    {
        std::uint8_t flags = 0;
        if (record.gamePersistent)
        {
            flags |= protocol::entity_lifecycle_flag::GamePersistent;
        }
        if (record.levelPersistent)
        {
            flags |= protocol::entity_lifecycle_flag::LevelPersistent;
        }
        if (record.creature)
        {
            flags |= protocol::entity_lifecycle_flag::Creature;
        }
        if (record.live)
        {
            flags |= protocol::entity_lifecycle_flag::Live;
        }
        if (record.available)
        {
            flags |= protocol::entity_lifecycle_flag::Available;
        }
        if (record.hasTransform)
        {
            flags |= protocol::entity_lifecycle_flag::HasTransform;
        }
        if (record.awaitingMaterialization)
        {
            flags |= protocol::entity_lifecycle_flag::
                AwaitingMaterialization;
        }
        if (record.hasVillageMembership)
        {
            flags |= protocol::entity_lifecycle_flag::
                HasVillageMembership;
        }
        return flags;
    }

    protocol::EntityLifecycleMessage WorldEntityDirectory::ToMessage(
        const WorldEntityRecord& record,
        protocol::EntityLifecycleOperation operation)
    {
        protocol::EntityLifecycleMessage message;
        message.operation = operation;
        message.flags = Flags(record);
        message.entityUid = record.thingUid;
        message.villageUid = record.hasVillageMembership
            ? record.villageUid
            : 0;
        message.entityGeneration = record.generation;
        message.worldRevision = record.worldRevision;
        message.simulationOwnerActorId = record.simulationOwnerActorId;
        message.mapEpoch = record.mapEpoch;
        message.mapId = record.mapId;
        message.definitionIndex = record.definitionIndex;
        if (record.hasTransform)
        {
            message.position = record.position;
            message.facing = record.facing;
        }
        message.mapName = record.mapName;
        message.definitionName = record.definitionName;
        message.scriptName = record.scriptName;
        if (record.hasLowSimulation)
        {
            message.lowSimulationRevision = record.lowSimulationRevision;
            message.lowSimulation = record.lowSimulation;
            message.lowSimulationFlags =
                (record.lowSimulation.respawnable ? 0x01u : 0u) |
                (record.lowSimulation.guard ? 0x02u : 0u) | 0x04u;
        }
        return message;
    }
}
