#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/PlayerActionMessage.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::multiplayer
{
    class UdpPeer;
}

namespace fable::multiplayer::entities
{
    class EntityPresenceReplication;
}

namespace fable::multiplayer::combat
{
    class PlayerCombatantDirectory;
}

namespace fable::multiplayer::replication
{
    class LocalHeroReplication;
    class RemotePlayerChannels;
}

namespace fable::multiplayer::presentation
{
    class RemotePlayerRegistry;

    // The action transport is reliable and ordered, but native presentation is
    // intentionally latest-current per actor/lane.  This prevents an old
    // animation from holding newer combat or ability state behind it.
    class RemotePlayerActionPresentation final
    {
    public:
        bool Initialize(
            UdpPeer& transport,
            replication::LocalHeroReplication& localHero,
            replication::RemotePlayerChannels& remoteChannels,
            RemotePlayerRegistry& remotePlayers,
            entities::EntityPresenceReplication& presence,
            combat::PlayerCombatantDirectory& combatants,
            const core::Diagnostics& diagnostics,
            std::uint64_t localActorId) noexcept;
        bool Offer(
            protocol::PlayerActionMessage message,
            std::uint64_t sourceConnectionNonce = 0);
        bool Process();
        void InvalidateActor(std::uint64_t actorId) noexcept;
        void InvalidateAllRemote() noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] static std::uint32_t DefaultDurationMs(
            protocol::PlayerActionKind kind) noexcept;
        // Finite one-shots are only replayable near their owner-authored
        // presentation point. Persistent ranged aim deliberately has no
        // expiry, while aim-end remains an ordered cancellation event.
        [[nodiscard]] static bool IsReplayEligible(
            protocol::PlayerActionKind kind,
            std::uint64_t sessionAgeMs,
            std::uint32_t expectedDurationMs) noexcept;
        [[nodiscard]] static bool IsRevisionNewer(
            std::uint32_t incoming,
            std::uint32_t current) noexcept;
        [[nodiscard]] static bool EnsureTiming(
            protocol::PlayerActionMessage& message,
            UdpPeer& transport,
            std::uint64_t localObservedAt,
            std::uint32_t durationMs,
            std::uint32_t& nextRevision) noexcept;

    private:
        enum class Lane : std::uint8_t
        {
            Combat = 0,
            HeroAbility = 1,
            Expression = 2,
            Count = 3,
        };

        struct Slot final
        {
            protocol::PlayerActionMessage message;
            std::uint64_t sourceConnectionNonce = 0;
            std::uint64_t offeredAtLocalMs = 0;
            std::uint64_t nativeReadyAtLocalMs = 0;
            bool active = false;
        };

        struct ActorSlots final
        {
            std::uint64_t actorId = 0;
            std::uint32_t latestRevision = 0;
            std::uint32_t authorityEpoch = 0;
            std::uint32_t actorGeneration = 0;
            std::uint32_t mapEpoch = 0;
            std::uint64_t sourceConnectionNonce = 0;
            std::array<Slot, static_cast<std::size_t>(Lane::Count)> lanes;
        };

        static constexpr std::size_t MaxActors = 256;
        static constexpr std::uint64_t TargetResolutionGraceMs = 1'000;
        static constexpr std::uint64_t NativeReadinessGraceMs = 2'000;
        static constexpr std::uint64_t NativeRetryMs = 50;

        [[nodiscard]] static Lane LaneFor(
            protocol::PlayerActionKind kind) noexcept;
        [[nodiscard]] ActorSlots* FindActor(std::uint64_t actorId) noexcept;
        [[nodiscard]] const ActorSlots* FindActor(
            std::uint64_t actorId) const noexcept;
        [[nodiscard]] ActorSlots* GetOrAllocateActor(
            std::uint64_t actorId) noexcept;
        void Retire(Slot& slot, const char* reason);
        UdpPeer* transport_ = nullptr;
        replication::LocalHeroReplication* localHero_ = nullptr;
        replication::RemotePlayerChannels* remoteChannels_ = nullptr;
        RemotePlayerRegistry* remotePlayers_ = nullptr;
        entities::EntityPresenceReplication* presence_ = nullptr;
        combat::PlayerCombatantDirectory* combatants_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::array<ActorSlots, MaxActors> actors_;
        std::uint64_t localActorId_ = 0;
        std::uint32_t nextRevision_ = 0;
        bool initialized_ = false;
    };
}
