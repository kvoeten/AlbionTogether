#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/NPC/Village/VillageMembershipMutationEvent.h"
#include "Multiplayer/Protocol/PlayerState.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

namespace fable::game::npc::village
{
    class VillageMembershipService;
}

namespace fable::multiplayer::authority
{
    class AuthorityReplication;
}

namespace fable::multiplayer::entities
{
    class EntityLifecycleReplication;
    class EntityNetworkIdentityRegistry;
    class LiveEntityRegistry;
}

namespace fable::multiplayer::replication
{
    // Captures explicit CTCVillageMember mutations and submits only the
    // current map owner's latest durable VillageUID to the host lifecycle
    // directory. Save hydration is ignored because the Thing is not live yet.
    class VillageMembershipReplication final
    {
    public:
        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            authority::AuthorityReplication& authority,
            entities::EntityLifecycleReplication& lifecycle,
            entities::EntityNetworkIdentityRegistry& identities,
            game::npc::village::VillageMembershipService& villages,
            const core::Diagnostics& diagnostics);
        bool Process(const entities::LiveEntityRegistry& liveEntities);
        void Shutdown() noexcept;

    private:
        static constexpr std::size_t EventCapacity = 1024;
        static constexpr std::uint64_t AuthorityGraceMilliseconds = 1'500;

        static void CaptureMutation(
            void* context,
            const game::npc::village::VillageMembershipMutationEvent& event);
        void Enqueue(
            const game::npc::village::VillageMembershipMutationEvent& event)
            noexcept;

        authority::AuthorityReplication* authority_ = nullptr;
        entities::EntityLifecycleReplication* lifecycle_ = nullptr;
        entities::EntityNetworkIdentityRegistry* identities_ = nullptr;
        game::npc::village::VillageMembershipService* villages_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::mutex eventMutex_;
        std::deque<game::npc::village::VillageMembershipMutationEvent>
            events_;
        std::deque<game::npc::village::VillageMembershipMutationEvent>
            deferred_;
        std::atomic_bool acceptingEvents_{false};
        std::atomic_uint droppedEvents_{0};
        unsigned int reportedDroppedEvents_ = 0;
        bool initialized_ = false;
    };
}
