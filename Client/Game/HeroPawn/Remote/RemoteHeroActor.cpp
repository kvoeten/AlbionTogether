#include "RemoteHeroActor.h"

#include "Game/Creature/Companion/Native/CompanionFunctions.h"
#include "Game/Creature/Animation/CreatureAnimationService.h"
#include "Game/Creature/Control/ScriptControl.h"
#include "Game/Creature/Look/CreatureLookService.h"
#include "Game/Creature/Locomotion/CreatureLocomotionService.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/HeroPawn/Appearance/Native/HeroMorphComponent.h"
#include "Game/HeroPawn/Abilities/HeroWillAbilityService.h"
#include "Game/NPC/NpcService.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Protocol/EquipmentTransitionTiming.h"
#include "RemoteHeroNativeLifecycle.h"

#include <Windows.h>

#include <cstdio>

namespace
{
    constexpr float kMinimumVisiblePlayerSeparation = 1.25f;
}

namespace fable::game::hero_pawn::remote
{
    using multiplayer::PlayerState;
    namespace movement = multiplayer::movement;

    RemoteHeroActor::RemoteHeroActor() = default;

    RemoteHeroActor::~RemoteHeroActor()
    {
        if (initialized_ || lifecycle_ != nullptr)
        {
            Shutdown();
        }
    }

    bool RemoteHeroActor::Initialize(
        game::EntityService& entities,
        game::NpcService& npcs,
        game::creature::locomotion::CreatureLocomotionService& locomotion,
        game::creature::look::CreatureLookService& look,
        game::creature::animation::CreatureAnimationService& animation,
        game::creature::combat::CreatureCombatService& combat,
        game::hero_pawn::abilities::HeroWillAbilityService& abilities,
        multiplayer::combat::PlayerCombatantDirectory& combatants,
        const core::Diagnostics& diagnostics,
        game::hero_pawn::appearance::hooks::
            RemoteHeroDefinitionHook& definitionHook,
        game::hero_pawn::appearance::hooks::
            RemoteHeroPresentationFactoryHook& presentationFactory,
        game::hero_pawn::equipment::hooks::
            RemoteRangedWeaponOrientationHook& orientationHook)
    {
        Shutdown();
        entities_ = &entities;
        npcs_ = &npcs;
        look_ = &look;
        combatants_ = &combatants;
        diagnostics_ = diagnostics;
        definitionHook_ = &definitionHook;
        presentationFactory_ = &presentationFactory;
        movement_.Initialize(locomotion, diagnostics);
        appearance_.Initialize(diagnostics);
        equipment_.Initialize(
            entities, animation, orientationHook, diagnostics);
        combat_.Initialize(entities, combat, equipment_, diagnostics);
        abilities_.Initialize(entities, abilities, diagnostics);
        expressions_.Initialize(entities, animation, diagnostics);
        lifecycle_ = std::make_unique<RemoteHeroNativeLifecycle>(*this);
        initialized_ = true;
        return true;
    }

    bool RemoteHeroActor::ApplyHealth(
        float currentHealth,
        float maximumHealth,
        std::uint32_t revision)
    {
        return initialized_ && IsLifecycleActive() && !worldTransitionActive_ &&
            combat_.ApplyHealth(currentHealth, maximumHealth, revision);
    }

    bool RemoteHeroActor::PerformAbility(
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
        return initialized_ && IsLifecycleActive() && !worldTransitionActive_ &&
            combat_.PerformAbility(
                weaponFamily,
                requiredWeapons,
                meleeAttachmentSlot,
                rangedAttachmentSlot,
                abilityId,
                charge,
                targetCreature, resolvedActionType, resolvedAnimationId);
    }

    bool RemoteHeroActor::EndRangedAim() noexcept
    {
        return initialized_ && IsLifecycleActive() && !worldTransitionActive_ &&
            combat_.EndRangedAim();
    }

    bool RemoteHeroActor::PerformWeaponTransition(
        game::creature::equipment::CreatureWeaponFamily weaponFamily,
        const game::hero_pawn::equipment::HeroWeaponDefinitions&
            requiredWeapons,
        std::uint32_t meleeAttachmentSlot,
        std::uint32_t rangedAttachmentSlot,
        const std::string& resolvedActionType,
        std::uint32_t resolvedAnimationId,
        std::uint64_t actionId)
    {
        game::hero_pawn::equipment::HeroEquipmentState equipment;
        equipment.valid = true;
        equipment.meleeDefinitionIndex =
            requiredWeapons.meleeDefinitionIndex;
        equipment.rangedDefinitionIndex =
            requiredWeapons.rangedDefinitionIndex;
        equipment.meleeAttachmentSlot = meleeAttachmentSlot;
        equipment.rangedAttachmentSlot = rangedAttachmentSlot;
        equipment.activeFamily = weaponFamily;
        equipment.transitionActionId = actionId;
        if (!initialized_ || !IsLifecycleActive() || worldTransitionActive_)
        {
            return false;
        }
        if (weaponFamily != game::creature::equipment::
                CreatureWeaponFamily::Ranged)
        {
            (void)combat_.EndRangedAim();
        }
        return equipment_.PerformTransition(
            equipment, resolvedActionType, resolvedAnimationId, actionId);
    }

    bool RemoteHeroActor::IsWeaponTransitionPending() const noexcept
    {
        return initialized_ && IsLifecycleActive() && !worldTransitionActive_ &&
            equipment_.IsTransitionPending();
    }

    bool RemoteHeroActor::PerformHeroAbility(
        game::hero_pawn::abilities::HeroAbility ability,
        game::hero_pawn::abilities::HeroAbilityCommand command,
        std::int32_t progressionState,
        void* targetCreature)
    {
        return initialized_ && IsLifecycleActive() && !worldTransitionActive_ &&
            abilities_.Perform(
                ability, command, progressionState, targetCreature);
    }

    bool RemoteHeroActor::PerformExpression(
        const std::string& expressionDefinition,
        void* targetCreature,
        const std::string& resolvedActionType,
        const std::uint32_t resolvedAnimationId,
        const std::int32_t expressionDurationTicks,
        const std::int32_t expressionTriggerTicks)
    {
        if (!initialized_ || !IsLifecycleActive() || worldTransitionActive_ ||
            nativeAvatar_ == nullptr || expressionDefinition.empty())
        {
            return false;
        }
        return expressions_.Perform(
            nativeAvatar_,
            targetCreature,
            expressionDefinition,
            resolvedActionType,
            resolvedAnimationId,
            expressionDurationTicks,
            expressionTriggerTicks);
    }

    movement::ReplicatedMovementSample
        RemoteHeroActor::MovementSample(
            const PlayerState& state,
            std::uint64_t receivedAt)
    {
        movement::ReplicatedMovementSample sample;
        sample.actorId = state.actorId;
        sample.authorityEpoch = state.authorityEpoch;
        sample.sequence = state.sequence;
        sample.mapName = state.mapName;
        sample.position = state.position;
        sample.velocity = state.velocity;
        sample.facing = state.facing;
        sample.angularVelocity = state.angularVelocity;
        sample.moving = state.moving;
        // The transport projects the owner-authored session timestamp onto
        // this receiver's monotonic clock. Fall back to arrival time during
        // the brief pre-synchronization window.
        sample.receivedAt = state.movementSampleAt != 0
            ? state.movementSampleAt
            : receivedAt;
        return sample;
    }

    void RemoteHeroActor::Reconcile(
        const PlayerState& state,
        const std::string& localMap,
        game::Entity* localHero,
        std::uint64_t receivedAt)
    {
        multiplayer::replication::RemotePlayerSnapshot snapshot;
        snapshot.state = state;
        snapshot.receivedAt = receivedAt;
        snapshot.lifecycle.actorGeneration = actorGeneration_;
        snapshot.lifecycle.mapEpoch = mapEpoch_;
        snapshot.lifecycle.appearancePresent =
            state.heroMorph.IsSane() && state.heroClothing.IsSane() &&
            state.heroBoneScales.IsSane() &&
            state.heroAppearanceModifiers.IsSane();
        snapshot.lifecycle.equipmentPresent = state.heroEquipment.IsSane();
        snapshot.lifecycle.appearanceReady =
            snapshot.lifecycle.appearancePresent;
        snapshot.lifecycle.equipmentReady = snapshot.lifecycle.equipmentPresent;
        snapshot.lifecycle.active = snapshot.lifecycle.appearanceReady &&
            snapshot.lifecycle.equipmentReady;
        Reconcile(snapshot, localMap, 0, localHero);
    }

    void RemoteHeroActor::Reconcile(
        const multiplayer::replication::RemotePlayerSnapshot& snapshot,
        const std::string& localMap,
        const std::uint16_t localMapId,
        game::Entity* localHero,
        const multiplayer::entities::LiveEntityRegistry* liveEntities)
    {
        const PlayerState& state = snapshot.state;
        const std::uint64_t receivedAt = snapshot.receivedAt;
        if (!initialized_ || worldTransitionActive_)
        {
            return;
        }
        bool nativePresenceMissing = false;
        if (liveEntities != nullptr && nativeAvatar_ != nullptr)
        {
            const auto* const live = liveEntities->FindByThing(nativeAvatar_);
            if (live != nullptr)
            {
                nativePresenceObserved_ = true;
            }
            else
            {
                nativePresenceMissing = nativePresenceObserved_;
            }
        }
        if (!snapshot.lifecycle.active)
        {
            // Defensive fence: the channel rejects mandatory-component
            // removal, but an inactive snapshot can still arrive from a
            // compatibility caller or a future lifecycle path. Never leave a
            // previously active native presentation actionable in that state.
            if (avatar_ != nullptr || lifecyclePhase_ ==
                    RemoteHeroLifecyclePhase::Active)
            {
                lifecycle_->Retire();
            }
            else
            {
                lifecyclePhase_ = RemoteHeroLifecyclePhase::Constructing;
                appearanceBaselineApplied_ = false;
                equipmentBaselineApplied_ = false;
            }
            return;
        }
        if (state.actorId == 0 || state.playerId.empty() ||
            state.appearanceDefinition.empty())
        {
            return;
        }
        const bool hasStableMapIdentity = state.mapId != 0 && localMapId != 0;
        const bool sharesLocalMap = hasStableMapIdentity
            ? state.mapId == localMapId
            : !state.mapName.empty() && state.mapName == localMap;
        if (!sharesLocalMap)
        {
            // The reliable channel retains the player and current state. A
            // native body belongs only to this world's matching incarnation;
            // do not retain hidden graphics/components across map changes.
            if (avatar_ != nullptr)
            {
                lifecycle_->Retire();
                diagnostics_.Event(
                    "MultiplayerRemoteAvatarLeftMap",
                    "native presentation retired; replicated actor state remains available for reunion");
            }
            actorId_ = state.actorId;
            actorGeneration_ = snapshot.lifecycle.actorGeneration;
            mapEpoch_ = snapshot.lifecycle.mapEpoch;
            return;
        }
        if (nativePresenceMissing)
        {
            diagnostics_.Event(
                "MultiplayerRemoteAvatarPresenceLost",
                "native CTCMapwho presence disappeared in the local map; re-materializing remote presentation");
            lifecycle_->Retire();
        }
        if (actorId_ != 0 && !MatchesLifecycle(
                snapshot.lifecycle.actorGeneration,
                snapshot.lifecycle.mapEpoch))
        {
            lifecycle_->Retire();
        }
        actorId_ = state.actorId;
        actorGeneration_ = snapshot.lifecycle.actorGeneration;
        mapEpoch_ = snapshot.lifecycle.mapEpoch;
        // The channel has already accepted a complete reliable construction
        // baseline. Native appearance and inventory application may continue
        // asynchronously, but they are independent of transform playback.
        const std::uint64_t now = GetTickCount64();
        if (avatar_ == nullptr && now < nextSpawnAttemptAt_)
        {
            return;
        }
        if (avatar_ == nullptr || !avatar_->IsValid() ||
            playerId_ != state.playerId ||
            appearanceDefinition_ != state.appearanceDefinition)
        {
            if (!lifecycle_->Spawn(state))
            {
                return;
            }
        }
        if (lifecyclePhase_ == RemoteHeroLifecyclePhase::Constructing)
        {
            const RemoteHeroActivationResult activation =
                lifecycle_->Activate(
                    state, localMap, localHero, receivedAt);
            if (activation == RemoteHeroActivationResult::Pending)
            {
                return;
            }
            if (activation == RemoteHeroActivationResult::Failed)
            {
                lifecycle_->Retire();
                return;
            }
        }
        movement_.Update(MovementSample(state, receivedAt), localMap);

        const bool appearanceReady = snapshot.lifecycle.appearancePresent &&
            snapshot.lifecycle.appearanceReady;
        const bool equipmentReady = snapshot.lifecycle.equipmentPresent &&
            snapshot.lifecycle.equipmentReady;
        if (!presentationStateReported_ && (appearanceReady || equipmentReady))
        {
            presentationStateReported_ = true;
            char detail[224] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "actor_id=%llu appearance=%s equipment=%s melee=%d ranged=%d",
                static_cast<unsigned long long>(actorId_),
                appearanceReady ? "ready" : "pending",
                equipmentReady ? "ready" : "pending",
                state.heroEquipment.meleeDefinitionIndex,
                state.heroEquipment.rangedDefinitionIndex);
            diagnostics_.Event(
                "MultiplayerRemotePresentationStateReceived", detail);
        }

        const auto appearanceResult = appearance_.Reconcile(
            state.heroMorph,
            state.heroClothing,
            state.heroBoneScales,
            state.heroAppearanceModifiers,
            appearanceReady);
        if (appearanceResult == game::hero_pawn::appearance::
                RemoteHeroAppearanceResult::Failed)
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-appearance-refresh");
            lifecycle_->Retire();
            return;
        }
        // Appearance resolution may span frames. Equipment is independent and
        // must continue reconciling while that work is pending.
        if (snapshot.lifecycle.equipmentPresent)
        {
            const auto& transition = snapshot.equipmentTransition;
            if (equipmentBaselineApplied_ && transition.IsPresent() &&
                multiplayer::protocol::equipment_transition_timing::EvaluateLocal(
                    now,
                    transition.startedAtLocalMs,
                    transition.durationMs) ==
                    multiplayer::protocol::equipment_transition_timing::Phase::Active)
            {
                const std::uint64_t elapsed64 =
                    now - transition.startedAtLocalMs;
                if (elapsed64 < transition.durationMs)
                {
                    (void)equipment_.PerformTransition(
                        state.heroEquipment,
                        transition.animationId,
                        transition.actionId,
                        static_cast<std::uint32_t>(elapsed64),
                        transition.durationMs,
                        transition.attachmentNotifyOffsetMs);
                }
            }
            equipment_.Reconcile(state.heroEquipment, now);
        }
        // The current Hero path requires both reliable component presence bits
        // before it can become Active. A default/native presentation is not a
        // valid replicated baseline.
        const bool appearanceApplied = snapshot.lifecycle.appearancePresent &&
            appearanceReady &&
            appearanceResult == game::hero_pawn::appearance::
                RemoteHeroAppearanceResult::Ready;
        const bool equipmentApplied = snapshot.lifecycle.equipmentPresent &&
            equipmentReady && equipment_.IsReady();
        appearanceBaselineApplied_ = appearanceApplied;
        equipmentBaselineApplied_ = equipmentApplied;
        if (appearanceApplied && equipmentApplied)
        {
            lifecyclePhase_ = RemoteHeroLifecyclePhase::AppearanceApplied;
            lifecyclePhase_ = RemoteHeroLifecyclePhase::Active;
        }
        else if (lifecyclePhase_ != RemoteHeroLifecyclePhase::Active)
        {
            // Native presentation readiness is an activation gate, not a
            // continuously recomputed actor lifecycle. A draw/stow action
            // temporarily marks equipment busy; once the complete baseline
            // has activated this actor, that must not make later real-time
            // actions look like stale pre-construction traffic.
            lifecyclePhase_ = RemoteHeroLifecyclePhase::NativeReady;
            return;
        }
        if (!separationReported_ && localHero != nullptr &&
            localHero->IsValid())
        {
            const game::Vector3 localPosition = localHero->GetPosition();
            const game::Vector3 remotePosition = avatar_->GetPosition();
            const float separation =
                localPosition.HorizontalDistanceTo(remotePosition);
            if (separation >= kMinimumVisiblePlayerSeparation)
            {
                separationReported_ = true;
                char detail[384] = {};
                std::snprintf(
                    detail, sizeof(detail),
                    "local=(%.3f,%.3f,%.3f) remote=(%.3f,%.3f,%.3f) separation=%.3f",
                    localPosition.x, localPosition.y, localPosition.z,
                    remotePosition.x, remotePosition.y, remotePosition.z,
                    separation);
                diagnostics_.Event(
                    "MultiplayerRemoteAvatarSeparated", detail);
            }
        }
    }

    bool RemoteHeroActor::ReadMovement(
        void* context,
        void* creature,
        movement::ReplicatedActorMovement::NativeInput& input)
    {
        auto* const presentation =
            static_cast<RemoteHeroActor*>(context);
        return presentation != nullptr && presentation->IsMovementReady() &&
            presentation->movement_.Provide(creature, input);
    }

    void RemoteHeroActor::BeginWorldTransition() noexcept
    {
        worldTransitionActive_ = true;
        if (lifecycle_ != nullptr)
        {
            lifecycle_->Retire();
        }
        diagnostics_.Event(
            "MultiplayerRemoteWorldPresentationRetired",
            "source native presentation released before world teardown; destination awaits its complete current baseline");
    }

    void RemoteHeroActor::DriveMovement()
    {
        if (lifecycle_ != nullptr)
        {
            lifecycle_->DriveMovement();
        }
    }

    bool RemoteHeroActor::IsMovementReady() const
    {
        return initialized_ &&
            lifecyclePhase_ != RemoteHeroLifecyclePhase::Constructing &&
            !worldTransitionActive_ && avatar_ != nullptr && avatar_->IsValid();
    }

    bool RemoteHeroActor::IsActive() const
    {
        return IsLifecycleActive() && avatar_ != nullptr &&
            !worldTransitionActive_ && avatar_->IsValid();
    }

    bool RemoteHeroActor::MatchesLifecycle(
        std::uint32_t actorGeneration,
        std::uint32_t mapEpoch) const noexcept
    {
        return actorGeneration_ == actorGeneration && mapEpoch_ == mapEpoch;
    }

    void RemoteHeroActor::CompleteWorldTransition() noexcept
    {
        worldTransitionActive_ = false;
        nextSpawnAttemptAt_ = 0;
    }

    void RemoteHeroActor::Shutdown() noexcept
    {
        if (lifecycle_ != nullptr)
        {
            lifecycle_->Retire();
        }
        movement_.Detach();
        abilities_.Shutdown();
        expressions_.Shutdown();
        combat_.Shutdown();
        equipment_.Shutdown();
        appearance_.Shutdown();
        lifecycle_.reset();
        entities_ = nullptr;
        npcs_ = nullptr;
        look_ = nullptr;
        combatants_ = nullptr;
        definitionHook_ = nullptr;
        presentationFactory_ = nullptr;
        diagnostics_ = {};
        nextSpawnAttemptAt_ = 0;
        actorId_ = 0;
        actorGeneration_ = 0;
        mapEpoch_ = 0;
        worldTransitionActive_ = false;
        initialized_ = false;
    }
}
