#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace fable::multiplayer::world
{
    // Bounded, current map identity table. It stores one retail map-id/name
    // pair per discovered map and never retains occupancy or transition history.
    class MapIdentityRegistry final
    {
    public:
        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            const core::Diagnostics& diagnostics);
        void Reconcile(
            const PlayerState* localPlayer,
            const std::vector<replication::RemotePlayerSnapshot>& remotePlayers);
        [[nodiscard]] const std::string* FindName(
            std::uint16_t mapId) const noexcept;
        [[nodiscard]] std::uint16_t FindId(
            const std::string& mapName) const noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;
        void Clear() noexcept;

    private:
        struct Binding final
        {
            std::string mapName;
            std::uint64_t sourceActorId = 0;
            bool hostVerified = false;
        };

        static constexpr std::size_t MaximumMapCount = 4096;

        bool Observe(const PlayerState& player, bool hostVerified);
        void ReportConflict(
            std::uint16_t mapId,
            const std::string& currentName,
            const std::string& candidateName,
            bool hostOverride);

        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        core::Diagnostics diagnostics_ = {};
        std::unordered_map<std::uint16_t, Binding> byId_;
        std::unordered_map<std::string, std::uint16_t> byName_;
        unsigned int conflictCount_ = 0;
    };
}
