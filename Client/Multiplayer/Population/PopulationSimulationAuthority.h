#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/NPC/Population/Hooks/PopulationSimulationHook.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Protocol/PopulationStateMessage.h"
#include "Multiplayer/Transport/ReliableMessageDispatcher.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace fable::game::npc::population
{
    class PopulationSimulationHook;
    enum class PopulationSimulationKind : std::uint8_t;
}

namespace fable::multiplayer::authority
{
    class AuthorityReplication;
}

namespace fable::multiplayer::replication
{
    class LocalHeroReplication;
}

namespace fable::multiplayer::population
{
    // The host owns low-detail world population. The current map lease owner
    // alone runs the retail high-detail spawn/despawn simulation.
    class PopulationSimulationAuthority final : public ReliableMessageSink
    {
    public:
        [[nodiscard]] ReliableMessageTypeSet HandledPacketTypes()
            const noexcept override
        {
            static constexpr protocol::PacketType types[] = {
                protocol::PacketType::PopulationState};
            return {types, sizeof(types) / sizeof(types[0])};
        }

        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            UdpPeer& transport,
            authority::AuthorityReplication& authority,
            replication::LocalHeroReplication& localHero,
            const core::Diagnostics& diagnostics);
        bool Attach(
            game::npc::population::PopulationSimulationHook& hook);
        bool Process();
        bool HandleReliableMessage(
            const TransportMessage& message) override;
        void SetHighDetailReady(
            const std::string& mapName,
            bool ready) noexcept;
        void Shutdown() noexcept;

    private:
        struct RegionState final
        {
            game::npc::population::PopulationSimulationState state;
            std::uint64_t revision = 0;
            bool valid = false;
            bool pending = false;
        };

        static constexpr std::uint8_t InvalidRegion = 0xFF;

        static bool ShouldExecute(
            void* context,
            game::npc::population::PopulationSimulationKind kind) noexcept;
        static void CaptureHostState(
            void* context,
            const game::npc::population::PopulationSimulationState& state)
            noexcept;
        static bool ProvideAuthoritativeState(
            void* context,
            game::npc::population::PopulationSimulationState& state)
            noexcept;
        [[nodiscard]] bool HasCurrentRegionState() noexcept;
        [[nodiscard]] static std::uint8_t RegionForMap(
            const std::string& mapName) noexcept;
        [[nodiscard]] static bool StatesEqual(
            const game::npc::population::PopulationSimulationState& left,
            const game::npc::population::PopulationSimulationState& right)
            noexcept;
        bool Submit(const protocol::PopulationStateMessage& message);

        authority::AuthorityReplication* authority_ = nullptr;
        replication::LocalHeroReplication* localHero_ = nullptr;
        game::npc::population::PopulationSimulationHook* hook_ = nullptr;
        UdpPeer* transport_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::uint64_t knownPeerRevision_ = 0;
        SRWLOCK stateLock_ = SRWLOCK_INIT;
        std::array<
            RegionState,
            protocol::PopulationStateMessage::RegionCount> regions_ = {};
        std::atomic_uint8_t currentRegion_{InvalidRegion};
        std::atomic_bool highDetailReady_{false};
    };
}
