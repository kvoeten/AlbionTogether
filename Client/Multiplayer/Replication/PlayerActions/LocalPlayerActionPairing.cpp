#include "LocalPlayerActionCapture.h"

#include "Game/HeroPawn/Equipment/Native/HeroWeaponComponent.h"
#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Replication/PlayerActionSemantics.h"

#include <Windows.h>

namespace fable::multiplayer::replication
{
    bool LocalPlayerActionCapture::PairAcceptedLocalActions()
    {
        PlayerActionEventQueue::Batch batch;
        eventQueue_.Drain(batch);
        auto& abilities = batch.abilities;
        auto& actions = batch.actions;
        auto& heroAbilities = batch.heroAbilities;
        auto& modeSources = batch.modeSources;
        while (!heroAbilities.empty())
        {
            if (!CaptureLocalHeroAbility(heroAbilities.front()))
            {
                return false;
            }
            heroAbilities.pop_front();
        }
        while (!abilities.empty())
        {
            unmatchedAbilities_.push_back(abilities.front());
            abilities.pop_front();
        }
        while (!actions.empty())
        {
            if (player_action_semantics::IsExpression(actions.front().actionType))
            {
                if (!CaptureLocalExpression(actions.front()))
                {
                    return false;
                }
            }
            else if (player_action_semantics::IsWeaponTransition(
                         actions.front().actionType))
            {
                pendingWeaponTransitions_.push_back(actions.front());
            }
            else if (player_action_semantics::IsRangedAimStart(
                         actions.front().actionType) ||
                     player_action_semantics::IsRangedFire(
                         actions.front().actionType))
            {
                if (!CaptureLocalRangedAction(actions.front()))
                {
                    return false;
                }
            }
            else
            {
                unmatchedActions_.push_back(actions.front());
            }
            actions.pop_front();
        }
        // Process mode exits after accepted actions from the same drain. A
        // fire action therefore remains ordered before the native mode exit.
        while (!modeSources.empty())
        {
            if (!CaptureLocalRangedAimEnd(modeSources.front()))
            {
                return false;
            }
            modeSources.pop_front();
        }
        while (unmatchedAbilities_.size() > PendingEventCapacity)
        {
            unmatchedAbilities_.pop_front();
        }
        while (unmatchedActions_.size() > PendingEventCapacity)
        {
            unmatchedActions_.pop_front();
        }
        while (pendingWeaponTransitions_.size() > PendingEventCapacity)
        {
            pendingWeaponTransitions_.pop_front();
        }

        const std::uint64_t now = GetTickCount64();
        void* const localHero = localHero_ != nullptr
            ? localHero_->NativeHero() : nullptr;
        for (auto transition = pendingWeaponTransitions_.begin();
             transition != pendingWeaponTransitions_.end();)
        {
            if (transition->creature == nullptr ||
                transition->creature != localHero || !transition->accepted)
            {
                transition = pendingWeaponTransitions_.erase(transition);
                continue;
            }
            const std::uint64_t lastMutationAt = localHero_ != nullptr
                ? localHero_->LastEquipmentMutationAt() : 0;
            const bool actionMutationObserved = transition->observedAt != 0 &&
                lastMutationAt >= transition->observedAt;
            if (!actionMutationObserved ||
                now < lastMutationAt + WeaponTransitionMutationSettleMilliseconds)
            {
                if (transition->observedAt != 0 &&
                    now > transition->observedAt +
                        WeaponTransitionCaptureWindowMilliseconds)
                {
                    diagnostics_.Event(
                        "MultiplayerLocalWeaponTransitionSuppressed",
                        "final-carry-state-not-observed");
                    transition = pendingWeaponTransitions_.erase(transition);
                    continue;
                }
                ++transition;
                continue;
            }
            game::hero_pawn::equipment::HeroEquipmentState equipment;
            const bool captured = game::hero_pawn::equipment::native::
                HeroWeaponComponent::Capture(localHero, equipment);
            const bool finalStateReady = captured && equipment.IsSane() &&
                (player_action_semantics::IsUnsheathe(transition->actionType)
                    ? equipment.activeFamily != game::creature::equipment::
                          CreatureWeaponFamily::None
                    : equipment.activeFamily == game::creature::equipment::
                          CreatureWeaponFamily::None);
            if (finalStateReady)
            {
                if (!CaptureLocalWeaponTransition(*transition, equipment))
                {
                    return false;
                }
                transition = pendingWeaponTransitions_.erase(transition);
                continue;
            }
            if (transition->observedAt != 0 &&
                now > transition->observedAt +
                    WeaponTransitionCaptureWindowMilliseconds)
            {
                diagnostics_.Event(
                    "MultiplayerLocalWeaponTransitionSuppressed",
                    "final-carry-state-not-observed");
                transition = pendingWeaponTransitions_.erase(transition);
                continue;
            }
            ++transition;
        }

        for (auto ability = unmatchedAbilities_.begin();
             ability != unmatchedAbilities_.end();)
        {
            if (ability->sourceCreature == nullptr ||
                ability->sourceCreature != localHero)
            {
                ability = unmatchedAbilities_.erase(ability);
                continue;
            }
            if (!ability->attackCommand)
            {
                if (!CaptureLocal(*ability))
                {
                    return false;
                }
                ability = unmatchedAbilities_.erase(ability);
                continue;
            }
            auto best = unmatchedActions_.end();
            std::uint64_t bestDistance = ActionPairWindowMilliseconds + 1;
            for (auto action = unmatchedActions_.begin();
                 action != unmatchedActions_.end(); ++action)
            {
                const bool exactCorrelation =
                    ability->actionCorrelationId != 0 &&
                    action->actionCorrelationId ==
                        ability->actionCorrelationId;
                const bool legacyCandidate =
                    ability->actionCorrelationId == 0 &&
                    action->actionCorrelationId == 0 &&
                    action->creature == ability->sourceCreature &&
                    action->threadId == ability->threadId;
                if (!exactCorrelation && !legacyCandidate)
                {
                    continue;
                }
                const std::uint64_t distance = action->observedAt >=
                    ability->observedAt
                    ? action->observedAt - ability->observedAt
                    : ability->observedAt - action->observedAt;
                if (exactCorrelation ||
                    (distance <= ActionPairWindowMilliseconds &&
                        distance < bestDistance))
                {
                    best = action;
                    bestDistance = distance;
                    if (exactCorrelation)
                    {
                        break;
                    }
                }
            }
            if (best != unmatchedActions_.end())
            {
                if (best->accepted)
                {
                    if (!CaptureLocal(*ability, &*best))
                    {
                        return false;
                    }
                }
                else
                {
                    diagnostics_.Event(
                        "MultiplayerLocalPlayerActionSuppressed",
                        "retail-action-rejected");
                }
                unmatchedActions_.erase(best);
                ability = unmatchedAbilities_.erase(ability);
                continue;
            }
            if (ability->observedAt != 0 &&
                now > ability->observedAt + ActionPairWindowMilliseconds)
            {
                diagnostics_.Event(
                    "MultiplayerLocalPlayerActionSuppressed",
                    "attack request had no accepted native Hero action");
                ability = unmatchedAbilities_.erase(ability);
                continue;
            }
            ++ability;
        }
        for (auto action = unmatchedActions_.begin();
             action != unmatchedActions_.end();)
        {
            if (action->observedAt == 0 ||
                now <= action->observedAt + ActionPairWindowMilliseconds)
            {
                ++action;
                continue;
            }
            action = unmatchedActions_.erase(action);
        }
        return true;
    }

}
