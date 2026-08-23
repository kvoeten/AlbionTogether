#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Math/Vector3.h"
#include "Multiplayer/Movement/ReplicatedActorMovement.h"
#include "Multiplayer/Protocol/EntityMovementMessage.h"
#include "Multiplayer/Protocol/PlayerState.h"

#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace fable::game::creature::locomotion
{
    class CreatureLocomotionService;
}

namespace fable::game::creature::look
{
    class CreatureLookService;
}

namespace fable::multiplayer
{
    class UdpPeer;
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

namespace fable::multiplayer::movement
{
    // Captures only the map owner's native creature frames and publishes one
    // lossy current sample per Thing. Host validation fences every sample by
    // map lease and host-issued entity generation before it is relayed.
    class EntityMovementReplication final
    {
    public:
        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            UdpPeer& transport,
            authority::AuthorityReplication& authority,
            entities::EntityLifecycleReplication& lifecycle,
            entities::EntityNetworkIdentityRegistry& identities,
            game::creature::locomotion::CreatureLocomotionService& locomotion,
            game::creature::look::CreatureLookService& look,
            const core::Diagnostics& diagnostics);
        bool Process(
            const entities::LiveEntityRegistry& liveEntities,
            const std::string& localMap,
            bool ownerRosterReady,
            std::uint64_t observerReadinessRevision);
        void Drive();
        void Shutdown() noexcept;

    private:
        static constexpr std::size_t CaptureCapacity = 4096;
        static constexpr std::uint64_t PublishIntervalMilliseconds = 50;
        static constexpr std::uint64_t DeferredSampleLifetimeMilliseconds =
            30'000;
        static constexpr std::size_t ReadinessReplayBudget = 64;

        struct NativeCapture final
        {
            std::uint64_t entityUid = 0;
            void* nativeThing = nullptr;
            game::Vector3 position = {};
            float facing = 0.0f;
            std::uint64_t capturedAt = 0;
        };

        struct LocalPublishState final
        {
            NativeCapture previous = {};
            protocol::EntityMovementMessage current = {};
            protocol::EntityMovementMessage published = {};
            std::uint64_t lastPublishedAt = 0;
            std::uint32_t nextSequence = 0;
            bool hasPrevious = false;
            bool hasCurrent = false;
            bool hasPublished = false;
        };

        struct CurrentSample final
        {
            protocol::EntityMovementMessage message = {};
            std::uint64_t receivedAt = 0;
        };

        struct Playback final
        {
            ReplicatedActorMovement movement;
            void* nativeThing = nullptr;
            std::uint32_t generation = 0;
            std::uint64_t ownerActorId = 0;
            std::uint32_t mapEpoch = 0;
            std::uint32_t sequence = 0;
        };

        static void ObserveCreatureFrame(void* context, void* creature);
        static bool ReadMovement(
            void* context,
            void* creature,
            ReplicatedActorMovement::NativeInput& input);
        void Capture(void* creature) noexcept;
        bool ProcessInbound();
        void RetryDeferredInbound();
        void PruneCurrentSamples();
        bool ProcessLocalCaptures(
            const entities::LiveEntityRegistry& liveEntities,
            const std::string& localMap,
            std::uint32_t mapEpoch);
        bool Accept(
            const protocol::EntityMovementMessage& message,
            std::uint64_t sourceActorId,
            std::uint64_t receivedAt,
            bool relay);
        bool Publish(protocol::EntityMovementMessage& message);
        void ReconcilePlaybacks(
            const entities::LiveEntityRegistry& liveEntities,
            const std::string& localMap);
        void RetirePlayback(std::uint64_t entityUid) noexcept;
        [[nodiscard]] static ReplicatedMovementSample ToSample(
            const CurrentSample& sample);
        [[nodiscard]] static bool IsNewerSequence(
            std::uint32_t candidate,
            std::uint32_t current) noexcept;
        [[nodiscard]] static std::uint32_t NextSequence(
            std::uint32_t current) noexcept;

        UdpPeer* transport_ = nullptr;
        authority::AuthorityReplication* authority_ = nullptr;
        entities::EntityLifecycleReplication* lifecycle_ = nullptr;
        entities::EntityNetworkIdentityRegistry* identities_ = nullptr;
        game::creature::locomotion::CreatureLocomotionService* locomotion_ =
            nullptr;
        game::creature::look::CreatureLookService* look_ = nullptr;
        HMODULE gameModule_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::mutex captureMutex_;
        std::unordered_map<std::uint64_t, NativeCapture> pendingCaptures_;
        std::unordered_map<std::uint64_t, LocalPublishState> localStates_;
        std::unordered_map<std::uint64_t, CurrentSample> currentSamples_;
        std::unordered_map<std::uint64_t, CurrentSample> deferredInbound_;
        std::unordered_map<std::uint64_t, std::unique_ptr<Playback>> playbacks_;
        std::unordered_set<std::uint64_t> publishedMovingEntities_;
        std::unordered_set<std::uint64_t> acceptedMovingEntities_;
        std::unordered_set<std::uint64_t> pendingReadinessReplays_;
        std::uint64_t knownPeerRevision_ = 0;
        std::uint64_t knownObserverReadinessRevision_ = 0;
        std::uint32_t rejectionReportCount_ = 0;
        std::string ownedMap_;
        std::uint32_t ownedMapEpoch_ = 0;
        std::atomic_bool captureEnabled_{false};
        std::atomic_bool initialized_{false};
    };
}
