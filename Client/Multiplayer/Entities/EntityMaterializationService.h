#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/PlayerState.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace fable::game
{
    class Entity;
    class EntityService;
}

namespace fable::game::npc::village
{
    class VillageMembershipService;
}

namespace fable::game::npc::simulation
{
    class DummyVillagerService;
}

namespace fable::multiplayer::authority
{
    class AuthorityReplication;
}

namespace fable::multiplayer::entities
{
    class EntityNetworkIdentityRegistry;
    class EntityPresenceReplication;
    class LiveEntityRegistry;
    class WorldEntityDirectory;
    struct LiveEntityRecord;
    struct WorldEntityRecord;

    // Reconciles the host's current map roster with this process's native
    // Things. Normal map occupants must come from the pre-construction native
    // saved simulation. Retail creation is reserved for explicit host-marked
    // arrivals such as a cross-map handoff.
    class EntityMaterializationService final
    {
    public:
        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            game::EntityService& entities,
            EntityPresenceReplication& presence,
            EntityNetworkIdentityRegistry& identities,
            game::npc::simulation::DummyVillagerService& dummyVillagers,
            game::npc::village::VillageMembershipService& villages,
            const core::Diagnostics& diagnostics);
        bool Reconcile(
            const WorldEntityDirectory& directory,
            const LiveEntityRegistry& liveEntities,
            const authority::AuthorityReplication& authority,
            const std::string& localMap,
            std::uint16_t localMapId);
        [[nodiscard]] bool IsLocalRosterReady(
            const std::string& localMap,
            std::uint32_t mapEpoch) const noexcept;
        void Shutdown() noexcept;

    private:
        struct PendingAttempt final
        {
            std::uint32_t generation = 0;
            std::uint64_t firstObservedAt = 0;
            std::uint64_t lastAttemptAt = 0;
            unsigned int attempts = 0;
        };

        struct OwnedPresentation final
        {
            game::Entity* entity = nullptr;
            std::uint64_t localUid = 0;
            std::uint32_t generation = 0;
        };

        static constexpr std::size_t MaximumTrackedEntities = 8192;
        static constexpr std::uint64_t ExceptionalArrivalGraceMilliseconds =
            500;
        static constexpr std::uint64_t RetryMilliseconds = 2'000;

        [[nodiscard]] static bool BelongsToMap(
            const WorldEntityRecord& record,
            const std::string& localMap,
            std::uint16_t localMapId) noexcept;
        [[nodiscard]] static bool ShouldExist(
            const WorldEntityRecord& record,
            const std::string& localMap,
            std::uint16_t localMapId) noexcept;
        bool EnsurePresent(
            const WorldEntityRecord& record,
            const WorldEntityDirectory& directory,
            const LiveEntityRegistry& liveEntities,
            const std::string& localMap,
            std::uint64_t now);
        bool AdoptByScriptIdentity(
            const WorldEntityRecord& record,
            const WorldEntityDirectory& directory,
            const LiveEntityRegistry& liveEntities,
            const std::string& localMap);
        bool AdoptBySimulationIdentity(
            const WorldEntityRecord& record,
            const LiveEntityRegistry& liveEntities,
            const std::string& localMap);
        bool Spawn(
            const WorldEntityRecord& record,
            const std::string& definitionName);
        bool ResolveDefinitionName(
            const WorldEntityRecord& record,
            std::string& definitionName);
        [[nodiscard]] bool EnsurePersistentState(
            const WorldEntityRecord& record,
            const LiveEntityRecord& live);
        [[nodiscard]] bool PersistentStateMatches(
            const WorldEntityRecord& record,
            const LiveEntityRecord& live) const noexcept;
        void ReconcileOwnedPresentations(
            const WorldEntityDirectory& directory,
            const LiveEntityRegistry& liveEntities,
            const std::string& localMap,
            std::uint16_t localMapId);
        void ReconcileRemovals(
            const WorldEntityDirectory& directory,
            const LiveEntityRegistry& liveEntities,
            const std::string& localMap,
            std::uint16_t localMapId,
            std::uint64_t now);
        [[nodiscard]] bool RosterMatches(
            const WorldEntityDirectory& directory,
            const LiveEntityRegistry& liveEntities,
            const std::string& localMap,
            std::uint16_t localMapId) const;
        void SetRosterReady(
            const std::string& localMap,
            std::uint32_t mapEpoch,
            bool ready);
        void ReleaseOwned(
            std::uint64_t canonicalUid,
            bool requestDestroy) noexcept;
        void Report(
            const char* event,
            const WorldEntityRecord& record,
            const char* result);

        game::EntityService* entities_ = nullptr;
        EntityPresenceReplication* presence_ = nullptr;
        EntityNetworkIdentityRegistry* identities_ = nullptr;
        game::npc::simulation::DummyVillagerService* dummyVillagers_ =
            nullptr;
        game::npc::village::VillageMembershipService* villages_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::uint64_t lastWorldRevision_ = 0;
        std::uint64_t nextRetryAt_ = 0;
        std::string lastMap_;
        std::string rosterMap_;
        std::uint32_t rosterEpoch_ = 0;
        std::unordered_map<std::uint64_t, PendingAttempt> pending_;
        std::unordered_map<std::uint64_t, OwnedPresentation> owned_;
        std::unordered_map<std::uint64_t, std::uint64_t> removalAttempts_;
        std::unordered_map<std::uint16_t, std::string> definitionNames_;
        unsigned int diagnosticCount_ = 0;
        bool rosterReady_ = false;
        bool initialized_ = false;
    };
}
