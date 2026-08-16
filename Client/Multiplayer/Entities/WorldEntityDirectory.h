#pragma once

#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Protocol/EntityLifecycleMessage.h"
#include "Multiplayer/Protocol/EntityMovementMessage.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace fable::multiplayer::entities
{
    struct WorldEntityRecord final
    {
        std::uint64_t thingUid = 0;
        std::uint64_t villageUid = 0;
        std::uint32_t generation = 0;
        std::uint64_t worldRevision = 0;
        std::uint64_t simulationOwnerActorId = 0;
        std::uint32_t mapEpoch = 0;
        std::uint32_t localIncarnation = 0;
        std::uint32_t seenBaselineId = 0;
        std::uint16_t mapId = 0;
        std::uint16_t definitionIndex = 0;
        game::Vector3 position = {};
        float facing = 0.0f;
        bool hasTransform = false;
        bool gamePersistent = false;
        bool levelPersistent = false;
        bool creature = false;
        bool live = false;
        bool available = true;
        bool awaitingMaterialization = false;
        bool hasVillageMembership = false;
        std::string mapName;
        std::string definitionName;
        std::string scriptName;
    };

    struct MapRosterCompletion final
    {
        std::uint64_t simulationOwnerActorId = 0;
        std::uint64_t worldRevision = 0;
        std::uint32_t mapEpoch = 0;
        std::string mapName;
    };

    struct MapRosterSeedPermission final
    {
        std::uint64_t simulationOwnerActorId = 0;
        std::uint32_t mapEpoch = 0;
    };

    // One current host-save projection record per authoritative Thing UID.
    // Transient retired records are erased; persistent dormant records remain.
    class WorldEntityDirectory final
    {
    public:
        bool HostObserve(
            const LiveEntityChange& change,
            const std::string& mapName,
            std::uint64_t simulationOwnerActorId,
            std::uint32_t mapEpoch,
            protocol::EntityLifecycleMessage& authoritative,
            bool& changed);
        bool HostApplyIntent(
            const protocol::EntityLifecycleMessage& intent,
            std::uint64_t simulationOwnerActorId,
            protocol::EntityLifecycleMessage& authoritative,
            bool& changed);
        bool HostApplyVillageMembershipMutation(
            const protocol::EntityLifecycleMessage& intent,
            std::uint64_t simulationOwnerActorId,
            protocol::EntityLifecycleMessage& authoritative,
            bool& changed);
        bool HostTransfer(
            const protocol::EntityLifecycleMessage& intent,
            std::uint64_t sourceActorId,
            const std::string& destinationMapName,
            std::uint64_t destinationOwnerActorId,
            std::uint32_t destinationMapEpoch,
            protocol::EntityLifecycleMessage& authoritative,
            bool& changed);
        bool HostResolveMapIdentity(
            std::uint64_t thingUid,
            const std::string& mapName,
            std::uint64_t simulationOwnerActorId,
            std::uint32_t mapEpoch,
            protocol::EntityLifecycleMessage& authoritative,
            bool& changed);
        bool HostReconcileMapAuthority(
            std::uint64_t thingUid,
            std::uint64_t simulationOwnerActorId,
            std::uint32_t mapEpoch,
            protocol::EntityLifecycleMessage& authoritative,
            bool& changed);
        bool HostCompleteMapRoster(
            const std::string& mapName,
            std::uint64_t simulationOwnerActorId,
            std::uint32_t mapEpoch,
            protocol::EntityLifecycleMessage& authoritative,
            bool& changed);
        bool ApplyAuthoritative(
            const protocol::EntityLifecycleMessage& message);
        bool HostAcceptMovement(
            const protocol::EntityMovementMessage& message) noexcept;

        [[nodiscard]] const WorldEntityRecord* Find(
            std::uint64_t thingUid) const noexcept;
        [[nodiscard]] const WorldEntityRecord* FindUniqueByScriptIdentity(
            const char* scriptName,
            std::uint16_t definitionIndex) const noexcept;
        [[nodiscard]] std::vector<WorldEntityRecord> Snapshot() const;
        [[nodiscard]] std::vector<MapRosterCompletion>
            CompletedMapRosters() const;
        [[nodiscard]] bool IsMapRosterComplete(
            const std::string& mapName,
            std::uint32_t mapEpoch) const noexcept;
        [[nodiscard]] bool HasMapRoster(
            const std::string& mapName) const noexcept;
        [[nodiscard]] bool IsMapSeedAllowed(
            const std::string& mapName,
            std::uint64_t simulationOwnerActorId,
            std::uint32_t mapEpoch) const noexcept;
        [[nodiscard]] bool HasAuthoritativeBaseline() const noexcept;
        [[nodiscard]] std::uint64_t LatestWorldRevision() const noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] static protocol::EntityLifecycleMessage ToMessage(
            const WorldEntityRecord& record,
            protocol::EntityLifecycleOperation operation);
        void Clear() noexcept;

    private:
        bool HostApplyCurrent(
            const WorldEntityRecord& observed,
            bool present,
            protocol::EntityLifecycleMessage& authoritative,
            bool& changed);
        bool ApplyBaselineBoundary(
            const protocol::EntityLifecycleMessage& message);
        [[nodiscard]] std::uint32_t NextGeneration() noexcept;
        [[nodiscard]] std::uint64_t NextWorldRevision() noexcept;
        static std::uint8_t Flags(const WorldEntityRecord& record) noexcept;
        std::unordered_map<std::uint64_t, WorldEntityRecord> records_;
        std::unordered_map<std::string, MapRosterCompletion>
            completedMapRosters_;
        std::unordered_map<std::string, MapRosterSeedPermission>
            mapSeedPermissions_;
        std::uint32_t nextGeneration_ = 0;
        std::uint64_t nextWorldRevision_ = 0;
        std::uint32_t activeBaselineId_ = 0;
        std::uint64_t activeBaselineRevision_ = 0;
        bool authoritativeBaselineReady_ = false;
    };
}
