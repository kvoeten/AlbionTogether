#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Authority/MapAuthorityCoordinator.h"
#include "Multiplayer/Presentation/RemotePlayerRegistry.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/LocalPlayerChannel.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <cstdint>

namespace fable::automation::runtime
{
    class RuntimeConfiguration;
}

namespace fable::game
{
    class EntityService;
    class NpcService;
}

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
    // Coordinates transport and world lifecycle. Owner capture, remote native
    // presentation, and actor locomotion live in their dedicated subsystems.
    class MultiplayerSession final
    {
    public:
        MultiplayerSession() = default;
        ~MultiplayerSession();

        MultiplayerSession(const MultiplayerSession&) = delete;
        MultiplayerSession& operator=(const MultiplayerSession&) = delete;

        bool Initialize(
            const automation::runtime::RuntimeConfiguration& configuration,
            game::EntityService& entities,
            game::NpcService& npcs,
            game::creature::locomotion::CreatureLocomotionService& locomotion,
            game::creature::look::CreatureLookService& look,
            const core::Diagnostics& diagnostics);
        bool OnWorldReady();
        // Returns true when the currently bound UE3 world started unloading.
        bool ProcessPresentationLifecycle();
        void DriveReplicatedMovement();
        void Shutdown() noexcept;

        [[nodiscard]] bool IsEnabled() const noexcept;
        [[nodiscard]] bool IsWorldReady() const noexcept;
        [[nodiscard]] bool HasActiveRemotePresentation() const;

    private:
        UdpPeer transport_;
        replication::LocalPlayerChannel localPlayerChannel_;
        replication::RemotePlayerChannels remotePlayerChannels_;
        replication::LocalHeroReplication localHero_;
        presentation::RemotePlayerRegistry remotePlayers_;
        authority::MapAuthorityCoordinator mapAuthority_;
        core::Diagnostics diagnostics_ = {};
        bool enabled_ = false;
        std::size_t reportedRemotePlayerCount_ = 0;
    };
}
