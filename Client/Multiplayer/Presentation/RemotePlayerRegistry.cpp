#include "RemotePlayerRegistry.h"

#include "Game/Entity/EntityService.h"
#include "Game/Creature/Animation/CreatureAnimationService.h"
#include "Game/HeroPawn/Remote/RemoteHeroActor.h"
#include "Game/HeroPawn/Abilities/HeroWillAbilityService.h"

#include <algorithm>
#include <cstdio>

namespace fable::multiplayer::presentation
{
    bool RemotePlayerRegistry::Initialize(
        game::EntityService& entities,
        game::NpcService& npcs,
        game::creature::locomotion::CreatureLocomotionService& locomotion,
        game::creature::look::CreatureLookService& look,
        game::creature::animation::CreatureAnimationService& animation,
        game::creature::combat::CreatureCombatService& combat,
        game::hero_pawn::abilities::HeroWillAbilityService& abilities,
        multiplayer::combat::PlayerCombatantDirectory& combatants,
        const core::Diagnostics& diagnostics,
        std::uint64_t localActorId)
    {
        Shutdown();
        entities_ = &entities;
        npcs_ = &npcs;
        locomotion_ = &locomotion;
        look_ = &look;
        animation_ = &animation;
        combat_ = &combat;
        abilities_ = &abilities;
        combatants_ = &combatants;
        diagnostics_ = diagnostics;
        localActorId_ = localActorId;
        if (!rangedOrientation_.Install(entities.GameModule(), diagnostics))
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-remote-ranged-orientation-hook");
            Shutdown();
            return false;
        }
        if (!presentationFactory_.Install(entities.GameModule(), diagnostics))
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-remote-hero-presentation-factory-hook");
            Shutdown();
            return false;
        }
        initialized_ = true;
        diagnostics_.Event(
            "MultiplayerRemotePlayerRegistryReady",
            "remote native presentations are dynamically keyed by actor lifecycle");
        return true;
    }

    std::unique_ptr<game::hero_pawn::remote::RemoteHeroActor>
        RemotePlayerRegistry::CreatePresentation()
    {
        auto presentation = std::make_unique<
            game::hero_pawn::remote::RemoteHeroActor>();
        if (!presentation->Initialize(
                *entities_, *npcs_, *locomotion_, *look_, *animation_,
                *combat_,
                *abilities_,
                *combatants_, diagnostics_,
                presentationFactory_, rangedOrientation_))
        {
            return nullptr;
        }
        return presentation;
    }

    void RemotePlayerRegistry::Reconcile(
        const std::vector<replication::RemotePlayerSnapshot>& snapshots,
        const std::string& localMap,
        game::Entity* localHero)
    {
        if (!initialized_)
        {
            return;
        }
        for (const replication::RemotePlayerSnapshot& snapshot : snapshots)
        {
            const PlayerState& state = snapshot.state;
            if (state.actorId == 0 || state.actorId == localActorId_)
            {
                continue;
            }
            auto iterator = presentations_.find(state.actorId);
            if (iterator == presentations_.end())
            {
                if (presentationFactory_.IsArmed())
                {
                    continue;
                }
                auto presentation = CreatePresentation();
                if (presentation == nullptr)
                {
                    diagnostics_.Event(
                        "ClientFailed",
                        "multiplayer-remote-presentation-allocation");
                    continue;
                }
                iterator = presentations_.emplace(
                    state.actorId, std::move(presentation)).first;
                char detail[128] = {};
                std::snprintf(
                    detail, sizeof(detail), "actor_id=%llu count=%zu",
                    static_cast<unsigned long long>(state.actorId),
                    presentations_.size());
                diagnostics_.Event(
                    "MultiplayerRemotePlayerRegistered", detail);
            }
            // The actor owns incarnation retirement and phase transitions;
            // passing the bounded lifecycle snapshot keeps generation/map
            // changes coupled to presentation teardown.
            iterator->second->Reconcile(snapshot, localMap, localHero);
        }
        // Cross-map actors remain in the channel and therefore stay dormant.
        // Reliable Retire removes the channel; prune its native presentation
        // in the same reconciliation pass so disconnects cannot leave ghosts.
        for (auto iterator = presentations_.begin();
             iterator != presentations_.end();)
        {
            const bool stillLive = std::any_of(
                snapshots.begin(), snapshots.end(),
                [actorId = iterator->first](
                    const replication::RemotePlayerSnapshot& snapshot)
                {
                    return snapshot.state.actorId == actorId;
                });
            if (stillLive)
            {
                ++iterator;
                continue;
            }
            iterator->second->Shutdown();
            iterator = presentations_.erase(iterator);
        }
    }

    void RemotePlayerRegistry::Remove(std::uint64_t actorId) noexcept
    {
        const auto iterator = presentations_.find(actorId);
        if (iterator == presentations_.end())
        {
            return;
        }
        iterator->second->Shutdown();
        presentations_.erase(iterator);
    }

    void RemotePlayerRegistry::BeginWorldTransition() noexcept
    {
        for (auto& [actorId, presentation] : presentations_)
        {
            (void)actorId;
            presentation->BeginWorldTransition();
        }
    }

    void RemotePlayerRegistry::CompleteWorldTransition() noexcept
    {
        for (auto& [actorId, presentation] : presentations_)
        {
            (void)actorId;
            presentation->CompleteWorldTransition();
        }
    }

    void RemotePlayerRegistry::DriveMovement()
    {
        for (auto& [actorId, presentation] : presentations_)
        {
            (void)actorId;
            presentation->DriveMovement();
        }
    }

    bool RemotePlayerRegistry::ApplyHealth(
        std::uint64_t actorId,
        float currentHealth,
        float maximumHealth,
        std::uint32_t revision)
    {
        const auto iterator = presentations_.find(actorId);
        return iterator != presentations_.end() &&
            iterator->second != nullptr &&
            iterator->second->ApplyHealth(
                currentHealth, maximumHealth, revision);
    }

    bool RemotePlayerRegistry::PerformAbility(
        std::uint64_t actorId,
        game::creature::equipment::CreatureWeaponFamily weaponFamily,
        const game::hero_pawn::equipment::HeroWeaponDefinitions&
            requiredWeapons,
        std::uint32_t meleeAttachmentSlot,
        std::uint32_t rangedAttachmentSlot,
        std::uint32_t abilityId,
        float charge,
        void* targetCreature,
        const std::string& resolvedActionType,
        std::uint32_t resolvedAnimationId)
    {
        const auto iterator = presentations_.find(actorId);
        return iterator != presentations_.end() &&
            iterator->second != nullptr &&
            iterator->second->PerformAbility(
                weaponFamily,
                requiredWeapons,
                meleeAttachmentSlot,
                rangedAttachmentSlot,
                abilityId,
                charge,
                targetCreature, resolvedActionType, resolvedAnimationId);
    }

    bool RemotePlayerRegistry::EndRangedAim(
        std::uint64_t actorId) noexcept
    {
        const auto iterator = presentations_.find(actorId);
        return iterator != presentations_.end() &&
            iterator->second != nullptr &&
            iterator->second->EndRangedAim();
    }

    bool RemotePlayerRegistry::PerformWeaponTransition(
        std::uint64_t actorId,
        game::creature::equipment::CreatureWeaponFamily weaponFamily,
        const game::hero_pawn::equipment::HeroWeaponDefinitions&
            requiredWeapons,
        std::uint32_t meleeAttachmentSlot,
        std::uint32_t rangedAttachmentSlot,
        const std::string& resolvedActionType,
        std::uint32_t resolvedAnimationId)
    {
        const auto iterator = presentations_.find(actorId);
        return iterator != presentations_.end() &&
            iterator->second != nullptr &&
            iterator->second->PerformWeaponTransition(
                weaponFamily,
                requiredWeapons,
                meleeAttachmentSlot,
                rangedAttachmentSlot,
                resolvedActionType,
                resolvedAnimationId);
    }

    bool RemotePlayerRegistry::PerformHeroAbility(
        std::uint64_t actorId,
        game::hero_pawn::abilities::HeroAbility ability,
        game::hero_pawn::abilities::HeroAbilityCommand command,
        std::int32_t progressionState,
        void* targetCreature)
    {
        const auto iterator = presentations_.find(actorId);
        return iterator != presentations_.end() &&
            iterator->second != nullptr &&
            iterator->second->PerformHeroAbility(
                ability, command, progressionState, targetCreature);
    }

    bool RemotePlayerRegistry::PerformExpression(
        const std::uint64_t actorId,
        const std::string& expressionDefinition,
        void* targetCreature,
        const std::string& resolvedActionType,
        const std::uint32_t resolvedAnimationId,
        const std::int32_t expressionDurationTicks,
        const std::int32_t expressionTriggerTicks)
    {
        const auto iterator = presentations_.find(actorId);
        return iterator != presentations_.end() &&
            iterator->second != nullptr &&
            iterator->second->PerformExpression(
                expressionDefinition,
                targetCreature,
                resolvedActionType,
                resolvedAnimationId,
                expressionDurationTicks,
                expressionTriggerTicks);
    }

    void RemotePlayerRegistry::Shutdown() noexcept
    {
        for (auto& [actorId, presentation] : presentations_)
        {
            (void)actorId;
            presentation->Shutdown();
        }
        presentations_.clear();
        rangedOrientation_.Shutdown();
        presentationFactory_.Cancel();
        presentationFactory_.Shutdown();
        entities_ = nullptr;
        npcs_ = nullptr;
        locomotion_ = nullptr;
        look_ = nullptr;
        animation_ = nullptr;
        combat_ = nullptr;
        abilities_ = nullptr;
        combatants_ = nullptr;
        diagnostics_ = {};
        localActorId_ = 0;
        initialized_ = false;
    }

    std::size_t RemotePlayerRegistry::Size() const noexcept
    {
        return presentations_.size();
    }

    std::size_t RemotePlayerRegistry::ActiveCount() const
    {
        return static_cast<std::size_t>(std::count_if(
            presentations_.begin(),
            presentations_.end(),
            [](const auto& entry)
            {
                return entry.second != nullptr && entry.second->IsActive();
            }));
    }

    bool RemotePlayerRegistry::IsLifecycleActive(
        const std::uint64_t actorId,
        const std::uint32_t actorGeneration,
        const std::uint32_t mapEpoch) const noexcept
    {
        const auto iterator = presentations_.find(actorId);
        return iterator != presentations_.end() &&
            iterator->second != nullptr &&
            iterator->second->IsLifecycleActive() &&
            iterator->second->MatchesLifecycle(actorGeneration, mapEpoch);
    }
}
