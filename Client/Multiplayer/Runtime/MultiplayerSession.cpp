#include "MultiplayerSession.h"

#include "Automation/Runtime/RuntimeConfiguration.h"
#include "Game/Entity/EntityService.h"

#include <Windows.h>

#include <cstdio>
#include <string>

namespace
{
    std::string Utf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }
        const int required = WideCharToMultiByte(
            CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1)
        {
            return {};
        }
        std::string result(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, value.c_str(), -1, result.data(), required, nullptr,
            nullptr);
        result.pop_back();
        return result;
    }

    std::uint64_t StablePlayerActorId(
        fable::multiplayer::PeerRole role,
        const std::string& playerId) noexcept
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (const unsigned char character : playerId)
        {
            hash ^= character;
            hash *= 1099511628211ull;
        }
        hash ^= static_cast<std::uint8_t>(role);
        hash *= 1099511628211ull;
        return hash == 0 ? 1 : hash;
    }
}

namespace fable::multiplayer
{
    MultiplayerSession::~MultiplayerSession()
    {
        Shutdown();
    }

    bool MultiplayerSession::Initialize(
        const automation::runtime::RuntimeConfiguration& configuration,
        game::EntityService& entities,
        game::NpcService& npcs,
        game::creature::locomotion::CreatureLocomotionService& locomotion,
        game::creature::look::CreatureLookService& look,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        if (!configuration.MultiplayerEnabled())
        {
            return true;
        }

        diagnostics_ = diagnostics;
        const PeerRole role = configuration.MultiplayerRole() == L"host"
            ? PeerRole::Host
            : PeerRole::Guest;
        const std::string playerId = Utf8(
            configuration.MultiplayerPlayerId());
        const std::string appearanceDefinition = Utf8(
            configuration.MultiplayerAppearance());
        const std::uint64_t localActorId = StablePlayerActorId(role, playerId);

        if (!remotePlayers_.Initialize(
                entities, npcs, locomotion, look, diagnostics_, localActorId))
        {
            Shutdown();
            return false;
        }
        const bool started = role == PeerRole::Host
            ? transport_.StartHost(
                configuration.MultiplayerPort(), diagnostics_)
            : transport_.StartGuest(
                Utf8(configuration.MultiplayerAddress()),
                configuration.MultiplayerPort(), diagnostics_);
        if (!started)
        {
            diagnostics_.Event("ClientFailed", "multiplayer-transport-start");
            Shutdown();
            return false;
        }
        localHero_.Initialize(
            entities, locomotion, localPlayerChannel_, transport_, diagnostics_,
            role, localActorId, playerId, appearanceDefinition,
            configuration.MorphSelfTest());
        mapAuthority_.Initialize(role, diagnostics_);
        enabled_ = true;

        char detail[320] = {};
        std::snprintf(
            detail, sizeof(detail),
            "role=%s player=%s actor_id=%llu authority_epoch=1 appearance=%s",
            role == PeerRole::Host ? "host" : "guest", playerId.c_str(),
            static_cast<unsigned long long>(localActorId),
            appearanceDefinition.c_str());
        diagnostics_.Event("MultiplayerSessionReady", detail);
        return true;
    }

    bool MultiplayerSession::OnWorldReady()
    {
        return !enabled_ || localHero_.OnWorldReady();
    }

    bool MultiplayerSession::ProcessPresentationLifecycle()
    {
        if (!enabled_)
        {
            return false;
        }
        if (!localHero_.IsWorldReady())
        {
            if (localHero_.IsEntryPending())
            {
                localHero_.TryBind();
            }
            if (localHero_.ConsumeCompletedWorldTransition())
            {
                remotePlayers_.CompleteWorldTransition();
            }
            return false;
        }
        if (localHero_.ConsumeCompletedWorldTransition())
        {
            remotePlayers_.CompleteWorldTransition();
        }
        if (!localHero_.WorldIsCurrent())
        {
            remotePlayers_.BeginWorldTransition();
            localHero_.BeginWorldTransition();
            return true;
        }

        const std::uint64_t now = GetTickCount64();
        localHero_.CaptureAppearance(now);
        PlayerState inbound;
        while (transport_.TryConsume(inbound))
        {
            if ((inbound.changedProperties & player_property::Retired) != 0)
            {
                remotePlayerChannels_.Remove(inbound.actorId);
                remotePlayers_.Remove(inbound.actorId);
                continue;
            }
            if (!remotePlayerChannels_.Apply(inbound, now))
            {
                continue;
            }
            if (remotePlayerChannels_.Size() > reportedRemotePlayerCount_)
            {
                reportedRemotePlayerCount_ = remotePlayerChannels_.Size();
                char detail[384] = {};
                std::snprintf(
                    detail, sizeof(detail),
                    "player=%s role=%s actor_id=%llu properties=0x%08X map=%s position=(%.3f,%.3f,%.3f) remote_count=%zu",
                    inbound.playerId.c_str(),
                    inbound.role == PeerRole::Host ? "host" : "guest",
                    static_cast<unsigned long long>(inbound.actorId),
                    inbound.changedProperties, inbound.mapName.c_str(),
                    inbound.position.x, inbound.position.y,
                    inbound.position.z, reportedRemotePlayerCount_);
                diagnostics_.Event("MultiplayerRemoteStateApplied", detail);
            }
        }

        const auto remoteSnapshots = remotePlayerChannels_.Snapshots();
        remotePlayers_.Reconcile(
            remoteSnapshots,
            localHero_.MapName(),
            localHero_.Hero());
        mapAuthority_.Reconcile(localHero_.CurrentState(), remoteSnapshots);
        return false;
    }

    void MultiplayerSession::DriveReplicatedMovement()
    {
        if (enabled_ && localHero_.IsWorldReady())
        {
            remotePlayers_.DriveMovement();
        }
    }

    void MultiplayerSession::Shutdown() noexcept
    {
        localHero_.Shutdown();
        remotePlayers_.Shutdown();
        localPlayerChannel_.Close();
        remotePlayerChannels_.Clear();
        mapAuthority_.Clear();
        transport_.Shutdown();
        diagnostics_ = {};
        enabled_ = false;
        reportedRemotePlayerCount_ = 0;
    }

    bool MultiplayerSession::IsEnabled() const noexcept
    {
        return enabled_;
    }

    bool MultiplayerSession::IsWorldReady() const noexcept
    {
        return localHero_.IsWorldReady();
    }

    bool MultiplayerSession::HasActiveRemotePresentation() const
    {
        return enabled_ && remotePlayers_.ActiveCount() != 0;
    }
}
