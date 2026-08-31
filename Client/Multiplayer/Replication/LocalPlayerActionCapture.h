#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Actions/CreatureActionLifecycleEvent.h"
#include "Game/Creature/Combat/CreatureAbilityEvent.h"
#include "Game/HeroPawn/Equipment/HeroEquipmentState.h"
#include "Game/HeroPawn/Abilities/HeroAbilityEvent.h"
#include "Multiplayer/Protocol/PlayerActionMessage.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Replication/PlayerActionEventQueue.h"

#include <cstddef>
#include <cstdint>
#include <deque>

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::game::creature::actions
{
    class CreatureActionLifecycleObserver;
}

namespace fable::game::creature::locomotion
{
    class CreatureModeManagerObserver;
}

namespace fable::game::hero_pawn::abilities
{
    class HeroWillAbilityService;
}

namespace fable::game
{
    class EntityService;
}

namespace fable::multiplayer
{
    class UdpPeer;
}

namespace fable::multiplayer::combat
{
    class PlayerCombatantDirectory;
}

namespace fable::multiplayer::entities
{
    class EntityNetworkIdentityRegistry;
}

namespace fable::multiplayer::replication
{
    class LocalHeroReplication;

    // Owns the bounded native callback ingress and the local semantic action
    // producer. It never admits remote messages or publishes transport data;
    // PlayerActionReplication drains its completed records after actor state
    // capture and applies the reliable ledger/publication policy.
    class LocalPlayerActionCapture final
    {
    public:
        void Initialize(
            PeerRole role,
            std::uint64_t localActorId,
            UdpPeer& transport,
            LocalHeroReplication& localHero,
            combat::PlayerCombatantDirectory& combatants,
            entities::EntityNetworkIdentityRegistry& identities,
            game::creature::combat::CreatureCombatService& combat,
            game::hero_pawn::abilities::HeroWillAbilityService& abilities,
            const core::Diagnostics& diagnostics);
        bool AttachActionObserver(
            game::creature::actions::CreatureActionLifecycleObserver& observer);
        bool AttachModeObserver(
            game::creature::locomotion::CreatureModeManagerObserver& observer);
        bool CapturePending();
        [[nodiscard]] bool IsInitialized() const noexcept
        {
            return initialized_;
        }
        [[nodiscard]] const protocol::PlayerActionMessage* PendingFront()
            const noexcept;
        void PopPending() noexcept;
        [[nodiscard]] std::size_t PendingCount() const noexcept
        {
            return pendingMessages_.size();
        }
        void Shutdown() noexcept;

    private:
        static constexpr std::size_t PendingEventCapacity = 1024;
        static constexpr std::size_t PendingMessageCapacity = 256;
        static constexpr std::uint64_t ActionPairWindowMilliseconds = 250;
        static constexpr std::uint64_t
            WeaponTransitionCaptureWindowMilliseconds = 1'500;
        static constexpr std::uint64_t
            WeaponTransitionMutationSettleMilliseconds = 100;
        static constexpr std::uint16_t WeaponTransitionDurationMilliseconds =
            1'000;
        static constexpr std::uint16_t WeaponAttachmentNotifyMilliseconds =
            200;

        static void CaptureAbility(
            void* context,
            const game::creature::combat::CreatureAbilityEvent& event);
        static void CaptureAction(
            void* context,
            const game::creature::actions::CreatureActionLifecycleEvent& event);
        static void CaptureHeroAbility(
            void* context,
            const game::hero_pawn::abilities::HeroAbilityEvent& event);
        static void CaptureModeSource(
            void* context,
            const game::creature::locomotion::CreatureModeSourceEvent& event);

        bool PairAcceptedLocalActions();
        bool CaptureLocal(
            const game::creature::combat::CreatureAbilityEvent& event,
            const game::creature::actions::CreatureActionLifecycleEvent*
                resolvedAction = nullptr);
        bool CaptureLocalWeaponTransition(
            const game::creature::actions::CreatureActionLifecycleEvent& action,
            const game::hero_pawn::equipment::HeroEquipmentState& equipment);
        bool CaptureLocalExpression(
            const game::creature::actions::CreatureActionLifecycleEvent& action);
        bool CaptureLocalRangedAction(
            const game::creature::actions::CreatureActionLifecycleEvent& action);
        bool CaptureLocalRangedAimEnd(
            const game::creature::locomotion::CreatureModeSourceEvent& event);
        bool CaptureLocalHeroAbility(
            const game::hero_pawn::abilities::HeroAbilityEvent& event);
        bool Queue(protocol::PlayerActionMessage message);
        bool EnsurePresentationTiming(
            protocol::PlayerActionMessage& message,
            std::uint64_t observedAt,
            std::uint32_t durationMs);
        [[nodiscard]] std::uint64_t NextActionId() noexcept;

        UdpPeer* transport_ = nullptr;
        LocalHeroReplication* localHero_ = nullptr;
        combat::PlayerCombatantDirectory* combatants_ = nullptr;
        entities::EntityNetworkIdentityRegistry* identities_ = nullptr;
        game::creature::combat::CreatureCombatService* combat_ = nullptr;
        game::hero_pawn::abilities::HeroWillAbilityService* abilities_ = nullptr;
        game::creature::actions::CreatureActionLifecycleObserver*
            actionObserver_ = nullptr;
        game::creature::locomotion::CreatureModeManagerObserver*
            modeObserver_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t localActorId_ = 0;
        std::uint64_t nextActionId_ = 0;
        PlayerActionEventQueue eventQueue_;
        std::deque<game::creature::combat::CreatureAbilityEvent>
            unmatchedAbilities_;
        std::deque<game::creature::actions::CreatureActionLifecycleEvent>
            unmatchedActions_;
        std::deque<game::creature::actions::CreatureActionLifecycleEvent>
            pendingWeaponTransitions_;
        std::deque<protocol::PlayerActionMessage> pendingMessages_;
        std::uint32_t nextPresentationRevision_ = 0;
        bool initialized_ = false;
    };
}
