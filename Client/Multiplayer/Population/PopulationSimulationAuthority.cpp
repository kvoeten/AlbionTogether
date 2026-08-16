#include "PopulationSimulationAuthority.h"

#include "Game/NPC/Population/Hooks/PopulationSimulationHook.h"
#include "Multiplayer/Authority/AuthorityReplication.h"
#include "Multiplayer/Protocol/PacketEnvelope.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Transport/TransportMessage.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <array>
#include <cstdio>

namespace fable::multiplayer::population
{
    void PopulationSimulationAuthority::Initialize(
        PeerRole role,
        std::uint64_t localActorId,
        UdpPeer& transport,
        authority::AuthorityReplication& authority,
        replication::LocalHeroReplication& localHero,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        transport_ = &transport;
        authority_ = &authority;
        localHero_ = &localHero;
        diagnostics_ = diagnostics;
    }

    bool PopulationSimulationAuthority::Attach(
        game::npc::population::PopulationSimulationHook& hook)
    {
        hook_ = &hook;
        hook_->SetExecutionSink(
            &PopulationSimulationAuthority::ShouldExecute,
            this);
        if (role_ == PeerRole::Host)
        {
            hook_->SetStateSink(
                &PopulationSimulationAuthority::CaptureHostState,
                this);
        }
        else
        {
            hook_->SetStateSource(
                &PopulationSimulationAuthority::ProvideAuthoritativeState,
                this);
        }
        diagnostics_.Event(
            "MultiplayerPopulationAuthorityReady",
            "host owns bounded Albion region targets; map owner consumes them in high-detail population");
        return hook_->IsInstalled();
    }

    void PopulationSimulationAuthority::SetHighDetailReady(
        const std::string& mapName,
        bool ready) noexcept
    {
        const std::uint8_t region = ready
            ? RegionForMap(mapName)
            : InvalidRegion;
        currentRegion_.store(region, std::memory_order_release);
        highDetailReady_.store(
            ready && (role_ == PeerRole::Host || region != InvalidRegion),
            std::memory_order_release);
    }

    bool PopulationSimulationAuthority::Process()
    {
        if (transport_ == nullptr)
        {
            return false;
        }
        if (role_ != PeerRole::Host)
        {
            return true;
        }

        const std::uint64_t peerRevision = transport_->PeerSetRevision();
        const bool baselineRequired = peerRevision != knownPeerRevision_;
        std::array<
            protocol::PopulationStateMessage,
            protocol::PopulationStateMessage::RegionCount> messages = {};
        std::size_t count = 0;
        AcquireSRWLockExclusive(&stateLock_);
        for (std::size_t index = 0; index < regions_.size(); ++index)
        {
            RegionState& slot = regions_[index];
            if (!slot.valid || (!slot.pending && !baselineRequired))
            {
                continue;
            }
            protocol::PopulationStateMessage& message = messages[count++];
            message.region = static_cast<std::uint8_t>(index);
            message.active = slot.state.active;
            message.revision = slot.revision;
            message.targetCounts = slot.state.targetCounts;
            message.regionFactors = slot.state.regionFactors;
            slot.pending = false;
        }
        ReleaseSRWLockExclusive(&stateLock_);

        for (std::size_t index = 0; index < count; ++index)
        {
            if (Submit(messages[index]))
            {
                continue;
            }
            AcquireSRWLockExclusive(&stateLock_);
            for (std::size_t restore = index; restore < count; ++restore)
            {
                regions_[messages[restore].region].pending = true;
            }
            ReleaseSRWLockExclusive(&stateLock_);
            return false;
        }
        knownPeerRevision_ = peerRevision;
        return true;
    }

    bool PopulationSimulationAuthority::HandleReliableMessage(
        const TransportMessage& transportMessage)
    {
        if (transportMessage.type != protocol::PacketType::PopulationState)
        {
            return false;
        }
        protocol::PopulationStateMessage message;
        if (!protocol::DecodePopulationStateMessage(
                transportMessage.payload.data(),
                transportMessage.payloadSize,
                message))
        {
            diagnostics_.Event(
                "MultiplayerPopulationStateRejected",
                "invalid host region-state payload");
            return true;
        }
        if (role_ == PeerRole::Host)
        {
            diagnostics_.Event(
                "MultiplayerPopulationStateRejected",
                "guest cannot publish Albion low-sim state");
            return true;
        }

        bool applied = false;
        AcquireSRWLockExclusive(&stateLock_);
        RegionState& slot = regions_[message.region];
        if (!slot.valid || message.revision > slot.revision)
        {
            slot.state.region = message.region;
            slot.state.active = message.active;
            slot.state.targetCounts = message.targetCounts;
            slot.state.regionFactors = message.regionFactors;
            slot.revision = message.revision;
            slot.valid = true;
            slot.pending = false;
            applied = true;
        }
        ReleaseSRWLockExclusive(&stateLock_);
        if (applied)
        {
            char detail[192] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "region=%u revision=%llu targets=(%d,%d,%d)",
                static_cast<unsigned int>(message.region),
                static_cast<unsigned long long>(message.revision),
                message.targetCounts[0],
                message.targetCounts[1],
                message.targetCounts[2]);
            diagnostics_.Event("MultiplayerPopulationStateApplied", detail);
        }
        return true;
    }

    void PopulationSimulationAuthority::Shutdown() noexcept
    {
        highDetailReady_.store(false, std::memory_order_release);
        if (hook_ != nullptr)
        {
            hook_->SetExecutionSink(nullptr, nullptr);
            hook_->SetStateSink(nullptr, nullptr);
            hook_->SetStateSource(nullptr, nullptr);
        }
        hook_ = nullptr;
        authority_ = nullptr;
        localHero_ = nullptr;
        transport_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        knownPeerRevision_ = 0;
        currentRegion_.store(InvalidRegion, std::memory_order_release);
        AcquireSRWLockExclusive(&stateLock_);
        regions_ = {};
        ReleaseSRWLockExclusive(&stateLock_);
    }

    bool PopulationSimulationAuthority::ShouldExecute(
        void* context,
        game::npc::population::PopulationSimulationKind kind) noexcept
    {
        auto* const policy = static_cast<PopulationSimulationAuthority*>(
            context);
        if (policy == nullptr)
        {
            return true;
        }
        if (kind == game::npc::population::
                PopulationSimulationKind::AlbionWorld)
        {
            return policy->role_ == PeerRole::Host;
        }
        if (policy->authority_ == nullptr || policy->localHero_ == nullptr ||
            !policy->localHero_->IsWorldReady() ||
            !policy->highDetailReady_.load(std::memory_order_acquire))
        {
            return false;
        }
        const std::string& mapName = policy->localHero_->MapName();
        const authority::MapAuthorityLease* const lease =
            policy->authority_->FindMapLease(mapName);
        const bool ownsMap = lease != nullptr && !mapName.empty() &&
            lease->epoch != 0 && lease->actorId == policy->localActorId_;
        return ownsMap && (policy->role_ == PeerRole::Host ||
            policy->HasCurrentRegionState());
    }

    void PopulationSimulationAuthority::CaptureHostState(
        void* context,
        const game::npc::population::PopulationSimulationState& state)
        noexcept
    {
        auto* const policy = static_cast<PopulationSimulationAuthority*>(
            context);
        if (policy == nullptr || policy->role_ != PeerRole::Host ||
            state.region >= policy->regions_.size())
        {
            return;
        }
        AcquireSRWLockExclusive(&policy->stateLock_);
        RegionState& slot = policy->regions_[state.region];
        if (!slot.valid || !StatesEqual(slot.state, state))
        {
            slot.state = state;
            ++slot.revision;
            if (slot.revision == 0)
            {
                ++slot.revision;
            }
            slot.valid = true;
            slot.pending = true;
        }
        ReleaseSRWLockExclusive(&policy->stateLock_);
    }

    bool PopulationSimulationAuthority::ProvideAuthoritativeState(
        void* context,
        game::npc::population::PopulationSimulationState& state) noexcept
    {
        auto* const policy = static_cast<PopulationSimulationAuthority*>(
            context);
        if (policy == nullptr || policy->role_ != PeerRole::Guest ||
            !policy->highDetailReady_.load(std::memory_order_acquire))
        {
            return false;
        }
        const std::uint8_t region = policy->currentRegion_.load(
            std::memory_order_acquire);
        if (region >= policy->regions_.size())
        {
            return false;
        }
        bool available = false;
        AcquireSRWLockShared(&policy->stateLock_);
        const RegionState& slot = policy->regions_[region];
        if (slot.valid)
        {
            state = slot.state;
            available = true;
        }
        ReleaseSRWLockShared(&policy->stateLock_);
        return available;
    }

    bool PopulationSimulationAuthority::HasCurrentRegionState() noexcept
    {
        const std::uint8_t region = currentRegion_.load(
            std::memory_order_acquire);
        if (region >= regions_.size())
        {
            return false;
        }
        bool available = false;
        AcquireSRWLockShared(&stateLock_);
        available = regions_[region].valid;
        ReleaseSRWLockShared(&stateLock_);
        return available;
    }

    std::uint8_t PopulationSimulationAuthority::RegionForMap(
        const std::string& mapName) noexcept
    {
        if (mapName == "GreatwoodEntrance" || mapName == "GreatwoodLake" ||
            mapName == "Greatwood" || mapName == "GreatwoodBanditToll")
        {
            return 0;
        }
        if (mapName == "Darkwood1" || mapName == "Darkwood2" ||
            mapName == "Darkwood3" || mapName == "Darkwood5" ||
            mapName == "Darkwood6")
        {
            return 1;
        }
        if (mapName == "Witchwood2" || mapName == "Witchwood4")
        {
            return 2;
        }
        return mapName == "LookoutPoint" ? 3 : InvalidRegion;
    }

    bool PopulationSimulationAuthority::StatesEqual(
        const game::npc::population::PopulationSimulationState& left,
        const game::npc::population::PopulationSimulationState& right)
        noexcept
    {
        return left.region == right.region && left.active == right.active &&
            left.targetCounts == right.targetCounts &&
            left.regionFactors == right.regionFactors;
    }

    bool PopulationSimulationAuthority::Submit(
        const protocol::PopulationStateMessage& message)
    {
        if (transport_ == nullptr)
        {
            return false;
        }
        std::array<std::uint8_t, protocol::MaximumDatagramBytes> payload = {};
        std::size_t payloadSize = 0;
        return protocol::EncodePopulationStateMessage(
                message,
                payload.data(),
                protocol::MaximumPayloadBytes(),
                payloadSize) &&
            transport_->SubmitReliable(
                protocol::PacketType::PopulationState,
                payload.data(),
                payloadSize);
    }
}
