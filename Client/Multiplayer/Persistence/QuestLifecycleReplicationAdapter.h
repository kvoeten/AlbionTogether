#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Quest/Runtime/QuestLifecycleEvent.h"
#include "Multiplayer/Protocol/PlayerState.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fable::game::quest
{
    class QuestLifecycleHook;
}

namespace fable::multiplayer::authority
{
    class AuthorityReplication;
}

namespace fable::multiplayer::replication
{
    class LocalHeroReplication;
    class RemotePlayerChannels;
}

namespace fable::multiplayer::protocol
{
    struct QuestMutationMessage;
    enum class QuestMutationOperation : std::uint8_t;
    enum class QuestMutationRejectionReason : std::uint8_t;
}

namespace fable::multiplayer::persistence
{
    class QuestMutationReplication;
    class QuestStateAuthorityService;

    // Bridges native quest lifecycle edges to the ordered semantic mutation
    // lane. It owns neither the hook nor transport and keeps load-time state
    // out of the event stream: the accepted quest snapshot is the baseline.
    class QuestLifecycleReplicationAdapter final
    {
    public:
        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            QuestMutationReplication& mutations,
            QuestStateAuthorityService& questState,
            authority::AuthorityReplication& authority,
            replication::LocalHeroReplication& localHero,
            replication::RemotePlayerChannels& remotePlayers,
            const core::Diagnostics& diagnostics) noexcept;
        bool Attach(game::quest::QuestLifecycleHook& hook) noexcept;
        bool Process();
        void Shutdown() noexcept;

    private:
        static constexpr std::size_t EventCapacity = 64;
        static constexpr std::size_t EventBudget = 16;

        static void OnLifecycleEvent(
            void* context,
            const game::quest::QuestLifecycleEvent& event) noexcept;
        static bool ValidateProposal(
            void* context,
            const protocol::QuestMutationMessage& proposal) noexcept;
        static bool ApplyCommit(
            void* context,
            const protocol::QuestMutationMessage& commit) noexcept;
        static void ObserveCommit(
            void* context,
            const protocol::QuestMutationMessage& commit,
            bool locallyOriginated) noexcept;
        static void ObserveRepair(
            void* context,
            std::uint64_t hostCampaignRevision,
            protocol::QuestMutationRejectionReason reason) noexcept;

        void QueueEvent(
            const game::quest::QuestLifecycleEvent& event) noexcept;
        [[nodiscard]] bool PeekEvent(
            game::quest::QuestLifecycleEvent& event) noexcept;
        void PopEvent() noexcept;
        void ClearEvents() noexcept;
        [[nodiscard]] bool RecoverFromEventOverflow(
            std::uint32_t droppedEvents);
        [[nodiscard]] bool SynchronizeCampaignBaseline() noexcept;
        [[nodiscard]] bool PublishEvent(
            const game::quest::QuestLifecycleEvent& event);
        [[nodiscard]] const PlayerState* FindPlayer(
            std::uint64_t actorId) const noexcept;
        [[nodiscard]] static protocol::QuestMutationOperation ToProtocol(
            game::quest::QuestLifecycleOperation operation) noexcept;
        [[nodiscard]] static game::quest::QuestLifecycleOperation ToNative(
            protocol::QuestMutationOperation operation) noexcept;
        void Report(const char* event, const char* detail) const noexcept;

        QuestMutationReplication* mutations_ = nullptr;
        QuestStateAuthorityService* questState_ = nullptr;
        authority::AuthorityReplication* authority_ = nullptr;
        replication::LocalHeroReplication* localHero_ = nullptr;
        replication::RemotePlayerChannels* remotePlayers_ = nullptr;
        game::quest::QuestLifecycleHook* hook_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::uint32_t boundAuthorityEpoch_ = 0;
        std::uint64_t boundSessionRevision_ = 0;
        std::uint64_t boundSnapshotRevision_ = 0;
        std::uint32_t repairAuthorityEpoch_ = 0;
        std::uint64_t repairSessionRevision_ = 0;
        std::uint64_t repairCampaignRevision_ = 0;
        std::array<game::quest::QuestLifecycleEvent, EventCapacity> events_ = {};
        SRWLOCK eventLock_ = SRWLOCK_INIT;
        std::size_t eventRead_ = 0;
        std::size_t eventCount_ = 0;
        game::quest::QuestLifecycleEvent overflowEvent_ = {};
        bool overflowEventPresent_ = false;
        std::atomic_uint32_t droppedEvents_{0};
        bool repairPending_ = false;
        bool liveEventsEnabled_ = false;
        bool initialized_ = false;
    };
}
