#include "EntityMaterializationService.h"

#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/NPC/Simulation/DummyVillager/DummyVillagerService.h"
#include "Game/NPC/Village/VillageMembershipService.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Entities/EntityPresenceReplication.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Entities/WorldEntityDirectory.h"

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fable::multiplayer::entities
{
    void EntityMaterializationService::Initialize(
        PeerRole role,
        std::uint64_t localActorId,
        game::EntityService& entities,
        EntityPresenceReplication& presence,
        EntityNetworkIdentityRegistry& identities,
        game::npc::simulation::DummyVillagerService& dummyVillagers,
        game::npc::village::VillageMembershipService& villages,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        entities_ = &entities;
        presence_ = &presence;
        identities_ = &identities;
        dummyVillagers_ = &dummyVillagers;
        villages_ = &villages;
        diagnostics_ = diagnostics;
        initialized_ = localActorId != 0;
        if (initialized_)
        {
            diagnostics_.Event(
                "MultiplayerEntityMaterializationReady",
                "native saved-map construction owns normal rosters; retail creature creation is reserved for explicit arrivals");
        }
    }

    bool EntityMaterializationService::Reconcile(
        const WorldEntityDirectory& directory,
        const LiveEntityRegistry& liveEntities,
        const authority::AuthorityReplication& authority,
        const std::string& localMap,
        std::uint16_t localMapId)
    {
        if (!initialized_ || entities_ == nullptr || presence_ == nullptr ||
            identities_ == nullptr || dummyVillagers_ == nullptr ||
            localMap.empty() || localMapId == 0)
        {
            return false;
        }

        const authority::MapAuthorityLease* const lease =
            authority.FindMapLease(localMap);
        if (lease == nullptr || lease->actorId == 0 || lease->epoch == 0)
        {
            SetRosterReady(localMap, 0, false);
            return true;
        }

        const bool ownsMap = lease->actorId == localActorId_;
        const bool baselineReady = role_ == PeerRole::Host ||
            directory.HasAuthoritativeBaseline();
        const bool canonicalRosterKnown = directory.HasMapRoster(localMap);
        const bool canonicalRosterCurrent = directory.IsMapRosterComplete(
            localMap,
            lease->epoch);
        const bool seedAllowed = role_ == PeerRole::Host ||
            directory.IsMapSeedAllowed(
                localMap,
                localActorId_,
                lease->epoch);
        if (ownsMap && (!baselineReady ||
                (canonicalRosterKnown && !canonicalRosterCurrent) ||
                (!canonicalRosterKnown && !seedAllowed)))
        {
            // A grant can arrive one or more reliable datagrams ahead of the
            // canonical roster for its epoch. Keep the retail map frozen until
            // the ordered boundary arrives; never seed the host from a stale
            // local save during this window.
            SetRosterReady(localMap, lease->epoch, false);
            return true;
        }

        const std::uint64_t now = GetTickCount64();
        const bool mapChanged = lastMap_ != localMap;
        const bool directoryChanged =
            lastWorldRevision_ != directory.LatestWorldRevision();
        if (!mapChanged && !directoryChanged && now < nextRetryAt_)
        {
            if (ownsMap &&
                (canonicalRosterCurrent ||
                    (!canonicalRosterKnown && seedAllowed)))
            {
                SetRosterReady(
                    localMap,
                    lease->epoch,
                    !canonicalRosterKnown || RosterMatches(
                            directory,
                            liveEntities,
                            localMap,
                            localMapId));
            }
            return true;
        }
        lastMap_ = localMap;
        lastWorldRevision_ = directory.LatestWorldRevision();
        nextRetryAt_ = now + RetryMilliseconds;

        ReconcileOwnedPresentations(
            directory,
            liveEntities,
            localMap,
            localMapId);

        const std::vector<WorldEntityRecord> world = directory.Snapshot();
        for (const WorldEntityRecord& record : world)
        {
            if (!ShouldExist(record, localMap, localMapId))
            {
                pending_.erase(record.thingUid);
                continue;
            }
            if (!EnsurePresent(
                    record,
                    directory,
                    liveEntities,
                    localMap,
                    now))
            {
                return false;
            }
        }
        // Adoption installs canonical UID aliases synchronously. Only remove
        // replica-local extras after every canonical record has had a chance
        // to adopt its matching retail Thing by script identity.
        ReconcileRemovals(
            directory,
            liveEntities,
            localMap,
            localMapId,
            now);
        SetRosterReady(
            localMap,
            lease->epoch,
            !ownsMap || (!canonicalRosterKnown && seedAllowed) ||
                (canonicalRosterCurrent && RosterMatches(
                    directory,
                    liveEntities,
                    localMap,
                    localMapId)));
        return true;
    }

    bool EntityMaterializationService::IsLocalRosterReady(
        const std::string& localMap,
        std::uint32_t mapEpoch) const noexcept
    {
        return initialized_ && rosterReady_ && mapEpoch != 0 &&
            rosterEpoch_ == mapEpoch && rosterMap_ == localMap;
    }

    bool EntityMaterializationService::BelongsToMap(
        const WorldEntityRecord& record,
        const std::string& localMap,
        std::uint16_t localMapId) noexcept
    {
        return (!record.mapName.empty() && record.mapName == localMap) ||
            (record.mapName.empty() && record.mapId != 0 &&
                record.mapId == localMapId);
    }

    bool EntityMaterializationService::ShouldExist(
        const WorldEntityRecord& record,
        const std::string& localMap,
        std::uint16_t localMapId) noexcept
    {
        return record.thingUid != 0 && record.generation != 0 &&
            record.available && record.creature &&
            BelongsToMap(record, localMap, localMapId) &&
            (record.live || record.awaitingMaterialization);
    }

    bool EntityMaterializationService::EnsurePresent(
        const WorldEntityRecord& record,
        const WorldEntityDirectory& directory,
        const LiveEntityRegistry& liveEntities,
        const std::string& localMap,
        std::uint64_t now)
    {
        const auto owned = owned_.find(record.thingUid);
        if (owned != owned_.end())
        {
            if (owned->second.generation == record.generation &&
                owned->second.entity != nullptr &&
                owned->second.entity->IsValid())
            {
                const LiveEntityRecord* const live = liveEntities.Find(
                    record.thingUid);
                if (live != nullptr && live->creature &&
                    !EnsurePersistentState(record, *live))
                {
                    Report(
                        "MultiplayerEntityPersistentStateDeferred",
                        record,
                        "village-membership-unresolved");
                }
                return true;
            }
            ReleaseOwned(record.thingUid, true);
        }

        const LiveEntityRecord* const live = liveEntities.Find(
            record.thingUid);
        if (live != nullptr && live->creature)
        {
            const bool definitionMatches =
                live->definitionIndex == record.definitionIndex;
            const bool scriptMatches = record.scriptName.empty() ||
                live->scriptName == record.scriptName;
            if (!definitionMatches || !scriptMatches)
            {
                Report(
                    "MultiplayerEntityMaterializationDeferred",
                    record,
                    "local-uid-collision");
                return true;
            }
            pending_.erase(record.thingUid);
            removalAttempts_.erase(record.thingUid);
            if (!EnsurePersistentState(record, *live))
            {
                Report(
                    "MultiplayerEntityPersistentStateDeferred",
                    record,
                    "village-membership-unresolved");
            }
            return true;
        }

        if (pending_.size() >= MaximumTrackedEntities &&
            pending_.find(record.thingUid) == pending_.end())
        {
            diagnostics_.Event(
                "MultiplayerEntityMaterializationOverflow",
                "bounded arrival reconciliation table is full");
            return false;
        }
        PendingAttempt& attempt = pending_[record.thingUid];
        if (attempt.generation != record.generation)
        {
            attempt = {};
            attempt.generation = record.generation;
            attempt.firstObservedAt = now;
        }
        if (AdoptBySimulationIdentity(record, liveEntities, localMap))
        {
            pending_.erase(record.thingUid);
            return true;
        }
        if (AdoptByScriptIdentity(
                record,
                directory,
                liveEntities,
                localMap))
        {
            pending_.erase(record.thingUid);
            return true;
        }
        std::string definitionName;
        if (!ResolveDefinitionName(record, definitionName))
        {
            Report(
                "MultiplayerEntityMaterializationDeferred",
                record,
                "definition-name-unavailable");
            return true;
        }
        if (!record.awaitingMaterialization)
        {
            // A complete host saved-map record was installed before retail
            // Thing construction. If an ordinary live roster member is not
            // present now, spawning a replacement would hide a persistence or
            // identity bug and can duplicate the native actor. Keep the roster
            // unready until Fable registers the expected Thing.
            Report(
                "MultiplayerEntityMaterializationDeferred",
                record,
                "waiting-for-native-saved-map-construction");
            return true;
        }
        if (now - attempt.firstObservedAt <
                ExceptionalArrivalGraceMilliseconds ||
            (attempt.lastAttemptAt != 0 &&
                now - attempt.lastAttemptAt < RetryMilliseconds))
        {
            return true;
        }
        attempt.lastAttemptAt = now;
        ++attempt.attempts;
        if (!record.hasTransform)
        {
            Report(
                "MultiplayerEntityMaterializationDeferred",
                record,
                "arrival-transform-unavailable");
            return true;
        }
        if (!Spawn(record, definitionName))
        {
            Report(
                "MultiplayerEntityMaterializationDeferred",
                record,
                "retail-create-failed");
            return true;
        }
        pending_.erase(record.thingUid);
        return true;
    }

    bool EntityMaterializationService::AdoptByScriptIdentity(
        const WorldEntityRecord& record,
        const WorldEntityDirectory& directory,
        const LiveEntityRegistry& liveEntities,
        const std::string& localMap)
    {
        if (presence_ == nullptr || record.scriptName.empty() ||
            (!record.mapName.empty() && record.mapName != localMap))
        {
            return false;
        }

        // Script identity is a fallback, never a search heuristic. Both the
        // canonical world and this process must have exactly one matching
        // script-name/definition pair before an alias is installed.
        const WorldEntityRecord* const canonical =
            directory.FindUniqueByScriptIdentity(
                record.scriptName.c_str(), record.definitionIndex);
        if (canonical == nullptr || canonical->thingUid != record.thingUid)
        {
            return false;
        }

        std::uint64_t localUid = 0;
        for (const LiveEntityRecord& candidate : liveEntities.Snapshot())
        {
            if (candidate.thing == nullptr || candidate.thingUid == 0 ||
                candidate.definitionIndex != record.definitionIndex ||
                candidate.scriptName != record.scriptName ||
                (candidate.mapId != 0 && record.mapId != 0 &&
                    candidate.mapId != record.mapId))
            {
                continue;
            }
            if (localUid != 0 && localUid != candidate.thingUid)
            {
                return false;
            }
            localUid = candidate.thingUid;
        }
        const bool adopted = localUid != 0 &&
            presence_->BindNetworkIdentity(record.thingUid, localUid);
        if (adopted)
        {
            Report(
                "MultiplayerEntityMaterialized",
                record,
                "adopted-retail-script-identity");
        }
        return adopted;
    }

    bool EntityMaterializationService::AdoptBySimulationIdentity(
        const WorldEntityRecord& record,
        const LiveEntityRegistry& liveEntities,
        const std::string& localMap)
    {
        if (dummyVillagers_ == nullptr || presence_ == nullptr ||
            record.thingUid == 0 ||
            (!record.mapName.empty() && record.mapName != localMap))
        {
            return false;
        }
        std::uint64_t localUid = 0;
        for (const LiveEntityRecord& candidate : liveEntities.Snapshot())
        {
            if (candidate.thing == nullptr || !candidate.creature ||
                candidate.thingUid == 0 ||
                candidate.definitionIndex != record.definitionIndex ||
                (candidate.mapId != 0 && record.mapId != 0 &&
                    candidate.mapId != record.mapId))
            {
                continue;
            }
            game::npc::simulation::DummyVillagerState lowSimulation;
            if (!dummyVillagers_->Read(candidate.thing, lowSimulation) ||
                !lowSimulation.componentPresent ||
                lowSimulation.creatureUid != record.thingUid)
            {
                continue;
            }
            if (localUid != 0 && localUid != candidate.thingUid)
            {
                return false;
            }
            localUid = candidate.thingUid;
        }
        if (localUid == 0 ||
            !presence_->BindNetworkIdentity(record.thingUid, localUid))
        {
            return false;
        }
        Report(
            "MultiplayerEntityMaterialized",
            record,
            "adopted-retail-low-sim-creature-uid");
        return true;
    }

    bool EntityMaterializationService::Spawn(
        const WorldEntityRecord& record,
        const std::string& definitionName)
    {
        game::Entity* const created = entities_->CreateCreature(
            definitionName,
            record.position,
            record.scriptName);
        if (created == nullptr || !created->IsValid())
        {
            if (created != nullptr)
            {
                created->Release();
            }
            return false;
        }

        const std::uint64_t localUid = created->GetUid();
        if (localUid == 0 ||
            !presence_->BindNetworkIdentity(record.thingUid, localUid))
        {
            created->RequestDestroy(false);
            created->Release();
            return false;
        }
        created->SetKillOnLevelUnload(true);
        if (record.hasTransform)
        {
            created->Teleport(record.position, record.facing, false);
        }

        OwnedPresentation presentation;
        presentation.entity = created;
        presentation.localUid = localUid;
        presentation.generation = record.generation;
        owned_[record.thingUid] = presentation;
        Report(
            "MultiplayerEntityMaterialized",
            record,
            "created-retail-creature");
        return true;
    }

    bool EntityMaterializationService::ResolveDefinitionName(
        const WorldEntityRecord& record,
        std::string& definitionName)
    {
        if (!record.definitionName.empty())
        {
            definitionName = record.definitionName;
            return true;
        }
        const auto cached = definitionNames_.find(record.definitionIndex);
        if (cached != definitionNames_.end())
        {
            definitionName = cached->second;
            return true;
        }
        if (!entities_->ResolveDefinitionName(
                record.definitionIndex,
                definitionName))
        {
            return false;
        }
        if (definitionNames_.size() < MaximumTrackedEntities)
        {
            definitionNames_[record.definitionIndex] = definitionName;
        }
        return true;
    }

    bool EntityMaterializationService::EnsurePersistentState(
        const WorldEntityRecord& record,
        const LiveEntityRecord& live)
    {
        if (live.thing == nullptr)
        {
            return false;
        }
        const std::uint64_t expectedVillageUid =
            record.hasVillageMembership ? record.villageUid : 0;
        bool changed = false;
        if (villages_ == nullptr || !villages_->ApplyAuthoritative(
                live.thing, expectedVillageUid, changed))
        {
            return false;
        }
        if (changed)
        {
            Report(
                "MultiplayerEntityPersistentStateApplied",
                record,
                "village-membership");
        }
        return true;
    }

    bool EntityMaterializationService::PersistentStateMatches(
        const WorldEntityRecord& record,
        const LiveEntityRecord& live) const noexcept
    {
        game::npc::village::native::VillageMembershipState state;
        if (villages_ == nullptr || !villages_->Read(live.thing, state))
        {
            return false;
        }
        const std::uint64_t expectedVillageUid =
            record.hasVillageMembership ? record.villageUid : 0;
        if (!state.componentPresent)
        {
            return expectedVillageUid == 0;
        }
        return state.villageUid == expectedVillageUid &&
            state.linkedVillageUid == expectedVillageUid;
    }

    void EntityMaterializationService::ReconcileOwnedPresentations(
        const WorldEntityDirectory& directory,
        const LiveEntityRegistry& liveEntities,
        const std::string& localMap,
        std::uint16_t localMapId)
    {
        std::vector<std::pair<std::uint64_t, bool>> stale;
        stale.reserve(owned_.size());
        for (const auto& entry : owned_)
        {
            const std::uint64_t canonicalUid = entry.first;
            const OwnedPresentation& presentation = entry.second;
            const WorldEntityRecord* const world = directory.Find(
                canonicalUid);
            const bool generationChanged = world != nullptr &&
                world->generation != presentation.generation;
            const bool shouldExist = world != nullptr &&
                ShouldExist(*world, localMap, localMapId);
            if (shouldExist && !generationChanged)
            {
                continue;
            }

            // A generation change must destroy the old native incarnation.
            // For ordinary map departure ReconcileRemovals owns destruction
            // when the registration is already visible; otherwise cover the
            // short CreateCreature-to-presence-queue window here.
            const bool requestDestroy = generationChanged || world == nullptr ||
                liveEntities.Find(canonicalUid) == nullptr;
            stale.emplace_back(canonicalUid, requestDestroy);
        }
        for (const auto& entry : stale)
        {
            ReleaseOwned(entry.first, entry.second);
            pending_.erase(entry.first);
            removalAttempts_.erase(entry.first);
        }
    }

    void EntityMaterializationService::ReconcileRemovals(
        const WorldEntityDirectory& directory,
        const LiveEntityRegistry& liveEntities,
        const std::string& localMap,
        std::uint16_t localMapId,
        std::uint64_t now)
    {
        struct ScriptIdentityCandidate final
        {
            std::uint64_t canonicalUid = 0;
            bool ambiguous = false;
        };

        std::unordered_map<
            std::string,
            std::unordered_map<std::uint16_t, ScriptIdentityCandidate>>
                canonicalScriptIdentities;
        for (const WorldEntityRecord& record : directory.Snapshot())
        {
            if (record.available && !record.scriptName.empty())
            {
                ScriptIdentityCandidate& candidate =
                    canonicalScriptIdentities[record.scriptName]
                        [record.definitionIndex];
                if (candidate.canonicalUid == 0)
                {
                    candidate.canonicalUid = record.thingUid;
                }
                else if (candidate.canonicalUid != record.thingUid)
                {
                    candidate.ambiguous = true;
                }
            }
        }
        const std::vector<LiveEntityRecord> live = liveEntities.Snapshot();
        for (const LiveEntityRecord& local : live)
        {
            if (!local.creature ||
                !LiveEntityRegistry::IsReplicable(local))
            {
                continue;
            }
            if (localMapId != 0 && local.mapId != 0 &&
                local.mapId != localMapId)
            {
                // The native mapwho can expose persistent quest actors from a
                // neighbouring or low-simulation map while the destination
                // finishes loading. They are outside this roster's authority
                // domain and must never be destroyed as destination extras.
                removalAttempts_.erase(local.thingUid);
                continue;
            }
            const WorldEntityRecord* const world = directory.Find(
                local.thingUid);
            if (world == nullptr)
            {
                // CTCDummyVillager persists the exact high-simulation
                // CreatureUID at +0x38. Prefer that durable native bridge over
                // every descriptive fallback when the host directory knows
                // the referenced incarnation.
                game::npc::simulation::DummyVillagerState lowSimulation;
                if (dummyVillagers_ != nullptr &&
                    dummyVillagers_->Read(local.thing, lowSimulation) &&
                    lowSimulation.componentPresent &&
                    lowSimulation.creatureUid != 0)
                {
                    const WorldEntityRecord* const durable = directory.Find(
                        lowSimulation.creatureUid);
                    if (durable != nullptr && durable->available &&
                        durable->definitionIndex == local.definitionIndex &&
                        BelongsToMap(*durable, localMap, localMapId) &&
                        presence_->BindNetworkIdentity(
                            durable->thingUid, local.thingUid))
                    {
                        removalAttempts_.erase(local.thingUid);
                        char detail[256] = {};
                        std::snprintf(
                            detail,
                            sizeof(detail),
                            "canonical_uid=%016llX local_uid=%016llX source=CTCDummyVillager.CreatureUID",
                            static_cast<unsigned long long>(
                                durable->thingUid),
                            static_cast<unsigned long long>(local.thingUid));
                        diagnostics_.Event(
                            "MultiplayerEntityIdentityAdopted",
                            detail);
                        continue;
                    }
                }

                // A reconstructed or save-derived Thing can initially have a
                // process-local UID. Bind it only when the host has one exact,
                // unambiguous persistent script identity for the definition.
                // Once bound, the authoritative record can explicitly move,
                // replace, or retire the actor on a later pass.
                if (!local.scriptName.empty())
                {
                    const auto scripts = canonicalScriptIdentities.find(
                        local.scriptName);
                    if (scripts != canonicalScriptIdentities.end())
                    {
                        const auto definition = scripts->second.find(
                            local.definitionIndex);
                        if (definition != scripts->second.end() &&
                            !definition->second.ambiguous &&
                            definition->second.canonicalUid != 0 &&
                            definition->second.canonicalUid != local.thingUid &&
                            presence_->BindNetworkIdentity(
                                definition->second.canonicalUid,
                                local.thingUid))
                        {
                            removalAttempts_.erase(local.thingUid);
                            char detail[320] = {};
                            std::snprintf(
                                detail,
                                sizeof(detail),
                                "canonical_uid=%016llX local_uid=%016llX map=%s script_name=%s definition_index=%u",
                                static_cast<unsigned long long>(
                                    definition->second.canonicalUid),
                                static_cast<unsigned long long>(
                                    local.thingUid),
                                localMap.c_str(),
                                local.scriptName.c_str(),
                                static_cast<unsigned int>(
                                    local.definitionIndex));
                            diagnostics_.Event(
                                "MultiplayerEntityIdentityAdopted",
                                detail);
                            continue;
                        }
                    }
                }

                // A roster is a positive description of authoritative
                // entities, not a deletion list. Retail low-simulation actors
                // and transient quest actors may be omitted. Only an explicit
                // authoritative record is allowed to destroy a retail Thing;
                // presentations created by this service are handled through
                // owned_ in ReconcileOwnedPresentations.
                removalAttempts_.erase(local.thingUid);
                continue;
            }
            const bool belongsToLocalMap = BelongsToMap(
                *world, localMap, localMapId);
            const bool definitionMatches =
                local.definitionIndex == world->definitionIndex;
            const bool scriptMatches = world->scriptName.empty() ||
                local.scriptName == world->scriptName;
            const bool identityMatches = definitionMatches && scriptMatches;
            if (world->available && belongsToLocalMap && identityMatches)
            {
                // `live` describes the canonical high-simulation state, not
                // identity or existence. During an authority handoff the old
                // owner can publish a persistent NPC as dormant immediately
                // before the successor loads that exact saved Thing. Keep the
                // matching local presentation; the new owner will promote the
                // same UID to high-sim without destructive churn.
                removalAttempts_.erase(local.thingUid);
                continue;
            }
            const bool identityMismatch = world->available &&
                belongsToLocalMap && !identityMatches;
            std::uint64_t& lastAttempt = removalAttempts_[local.thingUid];
            if (lastAttempt != 0 && now - lastAttempt < RetryMilliseconds)
            {
                continue;
            }
            lastAttempt = now;
            if (entities_->RequestDestroyNative(local.thing, false))
            {
                Report(
                    "MultiplayerEntityDematerializationRequested",
                    *world,
                    identityMismatch
                        ? "local-uid-collision"
                        : "canonical-roster-absent");
            }
        }
    }

    bool EntityMaterializationService::RosterMatches(
        const WorldEntityDirectory& directory,
        const LiveEntityRegistry& liveEntities,
        const std::string& localMap,
        std::uint16_t localMapId) const
    {
        const std::vector<WorldEntityRecord> world = directory.Snapshot();
        for (const WorldEntityRecord& expected : world)
        {
            if (!ShouldExist(expected, localMap, localMapId))
            {
                continue;
            }
            const LiveEntityRecord* const live = liveEntities.Find(
                expected.thingUid);
            if (live == nullptr || live->thing == nullptr || !live->creature ||
                live->definitionIndex != expected.definitionIndex ||
                (!expected.scriptName.empty() &&
                    live->scriptName != expected.scriptName) ||
                !PersistentStateMatches(expected, *live))
            {
                return false;
            }
        }

        // Extra retail Things do not make an authoritative roster incomplete.
        // Low-simulation and quest actors may be intentionally absent from the
        // high-simulation roster. Their removal requires an explicit directory
        // record; absence is never interpreted as a tombstone.
        return true;
    }

    void EntityMaterializationService::SetRosterReady(
        const std::string& localMap,
        std::uint32_t mapEpoch,
        bool ready)
    {
        const bool changed = rosterMap_ != localMap ||
            rosterEpoch_ != mapEpoch || rosterReady_ != ready;
        rosterMap_ = localMap;
        rosterEpoch_ = mapEpoch;
        rosterReady_ = ready;
        if (!changed || localMap.empty() || mapEpoch == 0)
        {
            return;
        }
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "map=%s epoch=%u ready=%s",
            localMap.c_str(),
            mapEpoch,
            ready ? "true" : "false");
        diagnostics_.Event("MultiplayerCanonicalRosterReady", detail);
    }

    void EntityMaterializationService::ReleaseOwned(
        std::uint64_t canonicalUid,
        bool requestDestroy) noexcept
    {
        const auto owned = owned_.find(canonicalUid);
        if (owned == owned_.end())
        {
            return;
        }
        if (presence_ != nullptr && owned->second.localUid != 0)
        {
            (void)presence_->RetireNetworkIdentity(
                canonicalUid,
                owned->second.localUid);
        }
        if (owned->second.entity != nullptr)
        {
            if (requestDestroy && owned->second.entity->IsValid())
            {
                owned->second.entity->RequestDestroy(false);
            }
            owned->second.entity->Release();
        }
        owned_.erase(owned);
    }

    void EntityMaterializationService::Report(
        const char* event,
        const WorldEntityRecord& record,
        const char* result)
    {
        const bool critical = event != nullptr &&
            std::strcmp(event, "MultiplayerEntityMaterialized") == 0;
        ++diagnosticCount_;
        if (diagnosticCount_ > 2048 && !critical)
        {
            return;
        }
        char detail[512] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "thing_uid=%016llX generation=%u map=%s map_id=%u definition_index=%u script_name=%s result=%s",
            static_cast<unsigned long long>(record.thingUid),
            record.generation,
            record.mapName.c_str(),
            static_cast<unsigned int>(record.mapId),
            static_cast<unsigned int>(record.definitionIndex),
            record.scriptName.c_str(),
            result);
        diagnostics_.Event(event, detail);
    }

    void EntityMaterializationService::Shutdown() noexcept
    {
        for (auto& entry : owned_)
        {
            if (entry.second.entity != nullptr)
            {
                entry.second.entity->Release();
            }
        }
        owned_.clear();
        pending_.clear();
        removalAttempts_.clear();
        definitionNames_.clear();
        entities_ = nullptr;
        presence_ = nullptr;
        identities_ = nullptr;
        dummyVillagers_ = nullptr;
        villages_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        lastWorldRevision_ = 0;
        nextRetryAt_ = 0;
        lastMap_.clear();
        rosterMap_.clear();
        rosterEpoch_ = 0;
        diagnosticCount_ = 0;
        rosterReady_ = false;
        initialized_ = false;
    }
}
