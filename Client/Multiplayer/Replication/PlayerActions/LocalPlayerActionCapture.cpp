#include "LocalPlayerActionCapture.h"

#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/Creature/Locomotion/Hooks/CreatureModeManagerObserver.h"
#include "Game/HeroPawn/Abilities/HeroWillAbilityService.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Entities/EntityNetworkIdentityRegistry.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"

#include <Windows.h>

namespace fable::multiplayer::replication
{
    void LocalPlayerActionCapture::Initialize(
        const PeerRole role,
        const std::uint64_t localActorId,
        UdpPeer& transport,
        LocalHeroReplication& localHero,
        combat::PlayerCombatantDirectory& combatants,
        entities::EntityNetworkIdentityRegistry& identities,
        game::creature::combat::CreatureCombatService& combat,
        game::hero_pawn::abilities::HeroWillAbilityService& abilities,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        role_ = role;
        localActorId_ = localActorId;
        transport_ = &transport;
        localHero_ = &localHero;
        combatants_ = &combatants;
        identities_ = &identities;
        combat_ = &combat;
        abilities_ = &abilities;
        diagnostics_ = diagnostics;
        eventQueue_.SetAccepting(true);
        if (!combat_->AddAbilitySink(
                &LocalPlayerActionCapture::CaptureAbility, this))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-player-ability-observer");
            Shutdown();
            return;
        }
        if (!abilities_->AddEventSink(
                &LocalPlayerActionCapture::CaptureHeroAbility, this))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-player-hero-ability-observer");
            Shutdown();
            return;
        }
        initialized_ = true;
    }

    bool LocalPlayerActionCapture::AttachActionObserver(
        game::creature::actions::CreatureActionLifecycleObserver& observer)
    {
        if (!initialized_ || !observer.IsInstalled() ||
            !observer.AddEventSink(
                &LocalPlayerActionCapture::CaptureAction, this))
        {
            return false;
        }
        actionObserver_ = &observer;
        return true;
    }

    bool LocalPlayerActionCapture::AttachModeObserver(
        game::creature::locomotion::CreatureModeManagerObserver& observer)
    {
        if (!initialized_ || !observer.IsInstalled() ||
            !observer.AddModeSourceEventSink(
                &LocalPlayerActionCapture::CaptureModeSource, this))
        {
            return false;
        }
        modeObserver_ = &observer;
        return true;
    }

    bool LocalPlayerActionCapture::CapturePending()
    {
        if (!initialized_ || transport_ == nullptr || localHero_ == nullptr ||
            combat_ == nullptr || abilities_ == nullptr)
        {
            return false;
        }
        return PairAcceptedLocalActions();
    }

    const protocol::PlayerActionMessage*
        LocalPlayerActionCapture::PendingFront() const noexcept
    {
        return pendingMessages_.empty() ? nullptr : &pendingMessages_.front();
    }

    void LocalPlayerActionCapture::PopPending() noexcept
    {
        if (!pendingMessages_.empty())
        {
            pendingMessages_.pop_front();
        }
    }

    void LocalPlayerActionCapture::DiscardPending() noexcept
    {
        eventQueue_.Clear();
        unmatchedAbilities_.clear();
        unmatchedActions_.clear();
        pendingWeaponTransitions_.clear();
        pendingMessages_.clear();
    }

    void LocalPlayerActionCapture::CaptureAbility(
        void* context,
        const game::creature::combat::CreatureAbilityEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<LocalPlayerActionCapture*>(context)->eventQueue_.
                Enqueue(event);
        }
    }

    void LocalPlayerActionCapture::CaptureAction(
        void* context,
        const game::creature::actions::CreatureActionLifecycleEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<LocalPlayerActionCapture*>(context)->eventQueue_.
                Enqueue(event);
        }
    }

    void LocalPlayerActionCapture::CaptureModeSource(
        void* context,
        const game::creature::locomotion::CreatureModeSourceEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<LocalPlayerActionCapture*>(context)->eventQueue_.
                Enqueue(event);
        }
    }

    void LocalPlayerActionCapture::CaptureHeroAbility(
        void* context,
        const game::hero_pawn::abilities::HeroAbilityEvent& event)
    {
        if (context != nullptr)
        {
            static_cast<LocalPlayerActionCapture*>(context)->eventQueue_.
                Enqueue(event);
        }
    }

    void LocalPlayerActionCapture::Shutdown() noexcept
    {
        eventQueue_.SetAccepting(false);
        if (actionObserver_ != nullptr)
        {
            actionObserver_->RemoveEventSink(
                &LocalPlayerActionCapture::CaptureAction, this);
        }
        if (modeObserver_ != nullptr)
        {
            modeObserver_->RemoveModeSourceEventSink(
                &LocalPlayerActionCapture::CaptureModeSource, this);
        }
        if (combat_ != nullptr)
        {
            combat_->RemoveAbilitySink(
                &LocalPlayerActionCapture::CaptureAbility, this);
        }
        if (abilities_ != nullptr)
        {
            abilities_->RemoveEventSink(
                &LocalPlayerActionCapture::CaptureHeroAbility, this);
        }
        DiscardPending();
        transport_ = nullptr;
        localHero_ = nullptr;
        combatants_ = nullptr;
        identities_ = nullptr;
        combat_ = nullptr;
        abilities_ = nullptr;
        actionObserver_ = nullptr;
        modeObserver_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        localActorId_ = 0;
        nextActionId_ = 0;
        nextPresentationRevision_ = 0;
        initialized_ = false;
    }
}
