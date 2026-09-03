#include "MapIdentityRegistry.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace fable::multiplayer::world
{
    std::size_t MapIdentityRegistry::ConflictSignatureHash::operator()(
        const ConflictSignature& signature) const noexcept
    {
        std::size_t hash = std::hash<std::uint16_t>{}(signature.mapId);
        const auto combine = [&hash](const std::string& value)
        {
            const std::size_t valueHash = std::hash<std::string>{}(value);
            hash ^= valueHash + static_cast<std::size_t>(0x9e3779b9) +
                (hash << 6U) + (hash >> 2U);
        };
        combine(signature.currentName);
        combine(signature.candidateName);
        hash ^= std::hash<bool>{}(signature.authoritativeOverride) +
            static_cast<std::size_t>(0x9e3779b9) + (hash << 6U) +
            (hash >> 2U);
        return hash;
    }

    void MapIdentityRegistry::Initialize(
        PeerRole role,
        std::uint64_t localActorId,
        const core::Diagnostics& diagnostics)
    {
        Clear();
        role_ = role;
        localActorId_ = localActorId;
        diagnostics_ = diagnostics;
    }

    void MapIdentityRegistry::Reconcile(
        const PlayerState* localPlayer,
        const std::vector<replication::RemotePlayerSnapshot>& remotePlayers)
    {
        if (localPlayer != nullptr)
        {
            // Script-facing Hero map names can lag the numeric map change
            // during native travel. Only the retail preparation/authority
            // seams call ObserveAuthoritative; ordinary player state remains
            // provisional even when it belongs to the host.
            Observe(*localPlayer, false);
        }
        std::vector<const PlayerState*> ordered;
        ordered.reserve(remotePlayers.size());
        for (const auto& remote : remotePlayers)
        {
            ordered.push_back(&remote.state);
        }
        std::sort(
            ordered.begin(),
            ordered.end(),
            [](const PlayerState* left, const PlayerState* right)
            {
                return left->actorId < right->actorId;
            });
        for (const PlayerState* player : ordered)
        {
            if (player != nullptr)
            {
                Observe(*player, false);
            }
        }
    }

    bool MapIdentityRegistry::ObserveAuthoritative(
        const std::string& mapName,
        const std::uint16_t mapId)
    {
        PlayerState observation;
        observation.actorId = localActorId_;
        observation.mapId = mapId;
        observation.mapName = mapName;
        return Observe(observation, true);
    }

    const std::string* MapIdentityRegistry::FindName(
        std::uint16_t mapId) const noexcept
    {
        const auto match = byId_.find(mapId);
        return match != byId_.end() ? &match->second.mapName : nullptr;
    }

    std::uint16_t MapIdentityRegistry::FindId(
        const std::string& mapName) const noexcept
    {
        const auto match = byName_.find(mapName);
        return match != byName_.end() ? match->second : 0;
    }

    std::size_t MapIdentityRegistry::Size() const noexcept
    {
        return byId_.size();
    }

    void MapIdentityRegistry::Clear() noexcept
    {
        byId_.clear();
        byName_.clear();
        reportedConflicts_.clear();
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        diagnostics_ = {};
        conflictCount_ = 0;
    }

    bool MapIdentityRegistry::Observe(
        const PlayerState& player,
        const bool authoritative)
    {
        if (player.actorId == 0 || player.mapId == 0 ||
            player.mapName.empty())
        {
            return false;
        }
        const auto idMatch = byId_.find(player.mapId);
        const auto nameMatch = byName_.find(player.mapName);
        if (idMatch != byId_.end() &&
            idMatch->second.mapName == player.mapName &&
            nameMatch != byName_.end() && nameMatch->second == player.mapId)
        {
            if (authoritative)
            {
                idMatch->second.authoritative = true;
                idMatch->second.sourceActorId = player.actorId;
            }
            return true;
        }

        const bool conflict = idMatch != byId_.end() ||
            nameMatch != byName_.end();
        if (conflict && (!authoritative ||
                (idMatch != byId_.end() &&
                    idMatch->second.authoritative)))
        {
            ReportConflict(
                player.mapId,
                idMatch != byId_.end()
                    ? idMatch->second.mapName
                    : player.mapName,
                player.mapName,
                false);
            return false;
        }
        if (byId_.size() >= MaximumMapCount && idMatch == byId_.end())
        {
            return false;
        }

        if (idMatch != byId_.end())
        {
            byName_.erase(idMatch->second.mapName);
            byId_.erase(idMatch);
        }
        if (nameMatch != byName_.end())
        {
            const std::uint16_t previousId = nameMatch->second;
            byName_.erase(nameMatch);
            byId_.erase(previousId);
        }
        Binding binding;
        binding.mapName = player.mapName;
        binding.sourceActorId = player.actorId;
        binding.authoritative = authoritative;
        byId_[player.mapId] = std::move(binding);
        byName_[player.mapName] = player.mapId;
        if (conflict)
        {
            ReportConflict(
                player.mapId,
                "provisional",
                player.mapName,
                true);
        }
        return true;
    }

    void MapIdentityRegistry::ReportConflict(
        std::uint16_t mapId,
        const std::string& currentName,
        const std::string& candidateName,
        const bool authoritativeOverride)
    {
        ConflictSignature signature;
        signature.mapId = mapId;
        signature.currentName = currentName;
        signature.candidateName = candidateName;
        signature.authoritativeOverride = authoritativeOverride;
        if (reportedConflicts_.find(signature) != reportedConflicts_.end() ||
            reportedConflicts_.size() >= MaximumConflictDiagnostics)
        {
            return;
        }
        reportedConflicts_.insert(std::move(signature));
        ++conflictCount_;
        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "map_id=%u current=%s candidate=%s authoritative_override=%s",
            static_cast<unsigned int>(mapId),
            currentName.c_str(),
            candidateName.c_str(),
            authoritativeOverride ? "true" : "false");
        diagnostics_.Event("MultiplayerMapIdentityConflict", detail);
    }
}
