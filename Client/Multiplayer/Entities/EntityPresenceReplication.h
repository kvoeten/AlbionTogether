#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Entity/Presence/Hooks/ThingPresenceObserver.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"

#include <atomic>
#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace fable::multiplayer::entities
{
    class EntityNetworkIdentityRegistry;

    // Bridges native lifecycle callbacks into the multiplayer game-thread
    // boundary. It deliberately owns no historical event log: both transient
    // queues are bounded and drained every game-thread reconciliation. Native
    // register/unregister order is retained because it defines map handoffs.
    class EntityPresenceReplication final
    {
    public:
        void Initialize(
            EntityNetworkIdentityRegistry& identities,
            const core::Diagnostics& diagnostics);
        bool Attach(
            game::entity::presence::ThingPresenceObserver& observer);
        bool ProcessPending();
        // Drains ordered current lifecycle changes. baselineRequired asks the
        // network layer to reconcile from LiveEntities() before publishing.
        void TakeChanges(
            std::vector<LiveEntityChange>& changes,
            bool& baselineRequired);
        bool BindNetworkIdentity(
            std::uint64_t canonicalUid,
            std::uint64_t localUid);
        bool UnregisterLocalPresence(std::uint64_t canonicalUid) noexcept;
        [[nodiscard]] const LiveEntityRegistry& LiveEntities() const noexcept;
        void Shutdown() noexcept;

    private:
        static constexpr std::size_t PendingEventCapacity = 8192;
        static constexpr unsigned int DiagnosticChangeLimit = 2048;

        static void CaptureEvent(
            void* context,
            const game::entity::presence::ThingPresenceEvent& event);
        void Enqueue(
            const game::entity::presence::ThingPresenceEvent& event) noexcept;
        void TrackChange(const LiveEntityChange& change);

        game::entity::presence::ThingPresenceObserver* observer_ = nullptr;
        EntityNetworkIdentityRegistry* identities_ = nullptr;
        LiveEntityRegistry liveEntities_;
        core::Diagnostics diagnostics_ = {};
        std::mutex pendingMutex_;
        std::deque<game::entity::presence::ThingPresenceEvent> pendingEvents_;
        std::deque<LiveEntityChange> pendingChanges_;
        std::atomic_bool acceptingEvents_{false};
        std::atomic_uint droppedEvents_{0};
        unsigned int reportedDroppedEvents_ = 0;
        unsigned int diagnosticChangeCount_ = 0;
        unsigned int identityCollisionCount_ = 0;
        bool baselineRequired_ = true;
        bool initialized_ = false;
    };
}
