#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Authority/ActionAuthorityCoordinator.h"
#include "Multiplayer/Authority/MapAuthorityBaselineGate.h"
#include "Multiplayer/Authority/MapAuthorityCoordinator.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"
#include "Multiplayer/World/MapIdentityRegistry.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace fable::multiplayer
{
    class UdpPeer;
}

namespace fable::multiplayer::authority
{
    // Owns reliable host-issued authority messages. MultiplayerSession only
    // supplies current occupancy; it does not resolve or serialize leases.
    class AuthorityReplication final : public ReliableMessageSink
    {
    public:
        [[nodiscard]] ReliableMessageTypeSet HandledPacketTypes()
            const noexcept override
        {
            static constexpr protocol::PacketType types[] = {
                protocol::PacketType::Authority};
            return {types, sizeof(types) / sizeof(types[0])};
        }

        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            UdpPeer& transport,
            const core::Diagnostics& diagnostics);
        bool Reconcile(
            const PlayerState* localPlayer,
            const std::vector<replication::RemotePlayerSnapshot>& remotePlayers);
        bool RequestMapPreparation(
            const std::string& mapName,
            std::uint16_t mapId);
        [[nodiscard]] bool IsMapPreparationReady(
            const std::string& mapName,
            std::uint16_t mapId) const noexcept;
        bool ProcessControl();
        void SetMapBaselineGate(
            MapAuthorityBaselineGate* gate) noexcept;
        bool HandleReliableMessage(
            const TransportMessage& message) override;
        bool RequestActionLease(
            const protocol::EntityActionMessage& intent,
            std::uint64_t sourceActorId,
            ActionAuthorityLease& grantedLease);
        bool ReleaseActionLease(
            const EntityAuthorityKey& entity,
            std::uint64_t requestingActorId,
            std::uint32_t actionEpoch);
        bool TouchActionLease(
            const EntityAuthorityKey& entity,
            std::uint64_t actorId,
            std::uint32_t actionEpoch) noexcept;
        [[nodiscard]] const MapAuthorityLease* FindMapLease(
            const std::string& mapName) const noexcept;
        [[nodiscard]] std::vector<MapAuthorityLease>
            MapLeases() const;
        [[nodiscard]] const ActionAuthorityLease* FindActionLease(
            const EntityAuthorityKey& entity) const noexcept;
        [[nodiscard]] bool IsMapPublisher(
            const std::string& mapName,
            std::uint64_t actorId,
            std::uint32_t mapEpoch) const noexcept;
        [[nodiscard]] bool IsEntityPublisher(
            const EntityAuthorityKey& entity,
            const std::string& mapName,
            std::uint64_t actorId,
            std::uint32_t mapEpoch) const noexcept;
        [[nodiscard]] bool HasEntityActionPublisher(
            const std::string& mapName,
            std::uint64_t actorId,
            std::uint32_t mapEpoch) const noexcept;
        [[nodiscard]] const std::string* ResolveMapName(
            std::uint16_t mapId) const noexcept;
        [[nodiscard]] std::uint16_t ResolveMapId(
            const std::string& mapName) const noexcept;
        [[nodiscard]] bool IsHost() const noexcept;
        void Shutdown() noexcept;

    private:
        struct BaselinePreparation final
        {
            std::uint16_t mapId = 0;
            std::uint64_t revision = 0;
            std::uint64_t requesterActorId = 0;
            std::string mapName;
            bool acknowledge = false;
        };

        bool RequestLocalMap(const PlayerState* localPlayer);
        bool SubmitMapRequest(
            protocol::AuthorityOperation operation,
            const std::string& mapName,
            std::uint16_t mapId,
            std::uint32_t observedEpoch);
        bool QueueHostBaselinePreparation(
            std::uint16_t mapId,
            const std::string& mapName,
            std::uint64_t requesterActorId,
            bool acknowledge);
        bool PublishHostBaselinePreparations();
        bool PublishHostMessages();

        MapAuthorityCoordinator maps_;
        ActionAuthorityCoordinator actions_;
        world::MapIdentityRegistry mapIdentities_;
        MapAuthorityBaselineGate* mapBaselineGate_ = nullptr;
        UdpPeer* transport_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::uint64_t knownPeerRevision_ = 0;
        std::string requestedLocalMap_;
        std::uint16_t preparedLocalMapId_ = 0;
        std::uint64_t preparedLocalBaselineRevision_ = 0;
        std::string preparedLocalMapName_;
        std::deque<BaselinePreparation> pendingBaselinePreparations_;
        std::unordered_map<std::uint64_t, std::string> actorMaps_;
        bool baselinePreparationDeferredReported_ = false;
        bool transportBackpressureReported_ = false;
        bool initialized_ = false;
    };
}
