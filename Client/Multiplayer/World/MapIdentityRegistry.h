#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Replication/RemotePlayerChannels.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
        // Retail map-change preparation supplies an exact map definition
        // name and numeric ID before script-facing Hero map labels settle.
        // Record that pair as authoritative so a stale post-load label cannot
        // split one native map into multiple multiplayer identities.
        bool ObserveAuthoritative(
            const std::string& mapName,
            std::uint16_t mapId);
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
            bool authoritative = false;
        };

        struct ConflictSignature final
        {
            std::uint16_t mapId = 0;
            std::string currentName;
            std::string candidateName;
            bool authoritativeOverride = false;

            bool operator==(const ConflictSignature& other) const noexcept
            {
                return mapId == other.mapId &&
                    currentName == other.currentName &&
                    candidateName == other.candidateName &&
                    authoritativeOverride == other.authoritativeOverride;
            }
        };

        struct ConflictSignatureHash final
        {
            std::size_t operator()(const ConflictSignature& signature) const noexcept;
        };

        static constexpr std::size_t MaximumMapCount = 4096;
        static constexpr std::size_t MaximumConflictDiagnostics = 32;

        bool Observe(const PlayerState& player, bool authoritative);
        void ReportConflict(
            std::uint16_t mapId,
            const std::string& currentName,
            const std::string& candidateName,
            bool authoritativeOverride);

        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        core::Diagnostics diagnostics_ = {};
        std::unordered_map<std::uint16_t, Binding> byId_;
        std::unordered_map<std::string, std::uint16_t> byName_;
        std::unordered_set<ConflictSignature, ConflictSignatureHash>
            reportedConflicts_;
        unsigned int conflictCount_ = 0;
    };
}
