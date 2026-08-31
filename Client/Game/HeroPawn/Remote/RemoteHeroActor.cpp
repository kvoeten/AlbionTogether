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
#include "Multiplayer/Protocol/EquipmentTransitionTiming.h"

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
        initialized_ = true;
        return true;
    }

    bool RemoteHeroActor::ApplyHealth(
        float currentHealth,
        float maximumHealth,
        std::uint32_t revision)
    {
        return initialized_ && IsLifecycleActive() && !avatarSuspended_ &&
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
        return initialized_ && IsLifecycleActive() && !avatarSuspended_ &&
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
        return initialized_ && IsLifecycleActive() && !avatarSuspended_ &&
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
        if (!initialized_ || !IsLifecycleActive() || avatarSuspended_)
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
        return initialized_ && IsLifecycleActive() && !avatarSuspended_ &&
            equipment_.IsTransitionPending();
    }

    bool RemoteHeroActor::PerformHeroAbility(
        game::hero_pawn::abilities::HeroAbility ability,
        game::hero_pawn::abilities::HeroAbilityCommand command,
        std::int32_t progressionState,
        void* targetCreature)
    {
        return initialized_ && IsLifecycleActive() && !avatarSuspended_ &&
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
        if (!initialized_ || !IsLifecycleActive() || avatarSuspended_ ||
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
        Reconcile(snapshot, localMap, localHero);
    }

    void RemoteHeroActor::Reconcile(
        const multiplayer::replication::RemotePlayerSnapshot& snapshot,
        const std::string& localMap,
        game::Entity* localHero)
    {
        const PlayerState& state = snapshot.state;
        const std::uint64_t receivedAt = snapshot.receivedAt;
        if (!initialized_)
        {
            return;
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
                Retire();
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
        if (state.mapName.empty() || state.mapName != localMap)
        {
            // A remote peer commonly crosses the boundary a few frames before
            // this process. Preserve its exact native presentation while the
            // peers are split; a map incarnation change is not a new player.
            actorId_ = state.actorId;
            actorGeneration_ = snapshot.lifecycle.actorGeneration;
            mapEpoch_ = snapshot.lifecycle.mapEpoch;
            Suspend(state, localMap);
            return;
        }
        const bool canReuseSuspendedPresentation =
            avatarSuspended_ && avatar_ != nullptr && avatar_->IsValid() &&
            actorId_ == state.actorId && playerId_ == state.playerId &&
            appearanceDefinition_ == state.appearanceDefinition;
        if (actorId_ != 0 && !MatchesLifecycle(
                snapshot.lifecycle.actorGeneration,
                snapshot.lifecycle.mapEpoch) &&
            !canReuseSuspendedPresentation)
        {
            Retire();
        }
        actorId_ = state.actorId;
        actorGeneration_ = snapshot.lifecycle.actorGeneration;
        mapEpoch_ = snapshot.lifecycle.mapEpoch;
        // The channel has already accepted a complete reliable construction
        // baseline. Native appearance and inventory application may continue
        // asynchronously, but they are independent of transform playback.
        if (avatarSuspended_ &&
            !Resume(state, localMap, localHero, receivedAt))
        {
            Retire();
        }
        const std::uint64_t now = GetTickCount64();
        if (avatar_ == nullptr && now < nextSpawnAttemptAt_)
        {
            return;
        }
        if (avatar_ == nullptr || !avatar_->IsValid() ||
            playerId_ != state.playerId ||
            appearanceDefinition_ != state.appearanceDefinition)
        {
            if (!Spawn(state))
            {
                return;
            }
        }
        if (lifecyclePhase_ == RemoteHeroLifecyclePhase::Constructing)
        {
            const RemoteHeroActivationResult activation =
                ActivateSpawnedPresentation(
                    state, localMap, localHero, receivedAt);
            if (activation == RemoteHeroActivationResult::Pending)
            {
                return;
            }
            if (activation == RemoteHeroActivationResult::Failed)
            {
                Retire();
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
            Retire();
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
            lifecyclePhase_ = RemoteHeroLifecyclePhase::BaselineApplied;
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

    bool RemoteHeroActor::Spawn(const PlayerState& state)
    {
        Retire();
        nextSpawnAttemptAt_ = GetTickCount64() + 5'000;
        definitionArmToken_ = definitionHook_->Arm();
        if (definitionArmToken_ == 0)
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-remote-runtime-definition-arm");
            return false;
        }
        factoryArmToken_ = presentationFactory_->Arm(state.position);
        avatar_ = npcs_->Spawn(
            state.appearanceDefinition, state.position,
            "SCRIPT_NAME_ALBION_TOGETHER_REMOTE_PLAYER");
        definitionHook_->Cancel(definitionArmToken_);
        definitionArmToken_ = 0;
        if (avatar_ == nullptr || !avatar_->IsValid())
        {
            diagnostics_.Event(
                "MultiplayerRemoteAvatarSpawnDeferred",
                "native destination construction is still settling; retrying the same actor baseline");
            Retire();
            return false;
        }
        if (avatar_->GetDefinitionName() != state.appearanceDefinition)
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-avatar-spawn");
            Retire();
            return false;
        }
        nativeAvatar_ = entities_->ResolveNative(avatar_->NativeHandle());
        if (!game::creature::native::CreatureFrameFunctions::ValidateCreature(
                entities_->GameModule(), nativeAvatar_))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-native-type");
            Retire();
            return false;
        }
        game::hero_pawn::appearance::native::HeroMorphResolutionState pending;
        const bool presentationAlreadyReady =
            game::hero_pawn::appearance::native::HeroMorphComponent::
                InspectResolution(nativeAvatar_, pending);
        (void)presentationAlreadyReady;
        if (pending.graphic != nullptr)
        {
            presentationFactory_->TargetGraphic(
                factoryArmToken_, pending.graphic);
        }
        appearance_.Bind(nativeAvatar_, actorId_);
        if (game::entity::native::ThingComponentAccess::Has(
                nativeAvatar_,
                game::entity::native::ThingComponentType::HeroMorph) &&
            !game::hero_pawn::appearance::native::HeroMorphComponent::
                SetUpdateRequested(nativeAvatar_, false))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-hero-morph-suppression");
            Retire();
            return false;
        }
        const bool appearanceReady = state.heroMorph.IsSane() &&
            state.heroClothing.IsSane() &&
            state.heroBoneScales.IsSane() &&
            state.heroAppearanceModifiers.IsSane();
        if (!appearance_.StageInitial(state.heroMorph, appearanceReady))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-morph-values");
            Retire();
            return false;
        }
        playerId_ = state.playerId;
        appearanceDefinition_ = state.appearanceDefinition;
        // A promoted Hero presentation owns asynchronous composite-texture and
        // bone-scaling work. Level-unload destruction can free those
        // components before the graphics queue consumes them, so keep it alive
        // through teardown. It is quarantined after unload; the destination
        // receives a fresh map-scoped presentation.
        if (!avatar_->SetKillOnLevelUnload(false) ||
            !avatar_->SetAttackable(true) ||
            !avatar_->SetDamageable(true) ||
            !avatar_->SetFriendsWithEverything(false) ||
            !avatar_->SetCollidable(true) || !avatar_->SetDrawable(true))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-presentation-flags");
            Retire();
            return false;
        }
        lifecyclePhase_ = RemoteHeroLifecyclePhase::Constructing;
        nextSpawnAttemptAt_ = 0;

        char detail[384] = {};
        std::snprintf(
            detail, sizeof(detail),
            "player=%s actor_id=%llu authority_epoch=%u definition=%s exact=true map=%s state=awaiting-native-presentation",
            playerId_.c_str(),
            static_cast<unsigned long long>(state.actorId),
            state.authorityEpoch, appearanceDefinition_.c_str(),
            state.mapName.c_str());
        diagnostics_.Event("MultiplayerRemoteDefinitionCreated", detail);
        return true;
    }

    RemoteHeroActivationResult
        RemoteHeroActor::ActivateSpawnedPresentation(
            const PlayerState& state,
            const std::string& localMap,
            game::Entity* localHero,
            std::uint64_t receivedAt)
    {
        game::hero_pawn::appearance::native::HeroMorphResolutionState
            presentation;
        if (!game::hero_pawn::appearance::native::HeroMorphComponent::
                InspectResolution(nativeAvatar_, presentation))
        {
            return RemoteHeroActivationResult::Pending;
        }
        // Hero-only components are part of the private runtime definition, so
        // these binds validate an already-complete graph. They must not add
        // components while Fable is still constructing the skeletal pawn.
        if (!equipment_.Bind(*avatar_, nativeAvatar_, actorId_))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-hero-equipment-bind");
            return RemoteHeroActivationResult::Failed;
        }
        if (!abilities_.Bind(nativeAvatar_, actorId_))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-hero-ability-bind");
            return RemoteHeroActivationResult::Failed;
        }
        if (!combat_.Bind(*avatar_, nativeAvatar_, actorId_))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-health-authority-fence");
            return RemoteHeroActivationResult::Failed;
        }
        nativeCompanionHero_ = localHero != nullptr && localHero->IsValid()
            ? entities_->ResolveNative(localHero->NativeHandle())
            : nullptr;
        game::creature::companion::native::CompanionRegistration companion;
        if (nativeCompanionHero_ == nullptr ||
            !game::creature::companion::native::CompanionFunctions::
                RegisterWithHero(
                    entities_->GameModule(),
                    nativeAvatar_,
                    nativeCompanionHero_,
                    companion))
        {
            char detail[256] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "actor_id=%llu avatar=%p hero=%p enemy=%p region_follower=%p",
                static_cast<unsigned long long>(actorId_),
                nativeAvatar_,
                nativeCompanionHero_,
                companion.followerEnemy,
                companion.heroRegionFollower);
            diagnostics_.Event(
                "MultiplayerRemoteCompanionRegistrationFailed", detail);
            return RemoteHeroActivationResult::Failed;
        }
        companionRegistered_ = true;
        char companionDetail[320] = {};
        std::snprintf(
            companionDetail,
            sizeof(companionDetail),
            "actor_id=%llu avatar=%p hero=%p enemy=%p region_follower=%p count_before=%u count_after=%u existing=%s locomotion=replicated",
            static_cast<unsigned long long>(actorId_),
            nativeAvatar_,
            nativeCompanionHero_,
            companion.followerEnemy,
            companion.heroRegionFollower,
            companion.followerCountBefore,
            companion.followerCountAfter,
            companion.alreadyRegistered ? "true" : "false");
        diagnostics_.Event(
            "MultiplayerRemoteCompanionRegistered", companionDetail);
        control_ = npcs_->TakeControl(avatar_, game::AiPriority::Highest);
        if (control_ == nullptr || !control_->ClearCommands())
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-avatar-control");
            return RemoteHeroActivationResult::Failed;
        }
        movement_.Bind(
            *avatar_, nativeAvatar_, MovementSample(state, receivedAt),
            localMap);
        if (!look_->RouteReplicatedMovement(
                avatar_, &RemoteHeroActor::ReadMovement, this))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-movement-routing");
            return RemoteHeroActivationResult::Failed;
        }

        char detail[384] = {};
        std::snprintf(
            detail, sizeof(detail),
            "player=%s actor_id=%llu authority_epoch=%u definition=%s exact=true map=%s control=scripted",
            playerId_.c_str(),
            static_cast<unsigned long long>(state.actorId),
            state.authorityEpoch, appearanceDefinition_.c_str(),
            state.mapName.c_str());
        diagnostics_.Event("MultiplayerRemoteAvatarReady", detail);
        diagnostics_.Event(
            "MultiplayerPresentationChannelOpened",
            "remote native creature presentation is scoped to the shared map lifecycle");
        if (!combatants_->Bind(actorId_, nativeAvatar_))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-combatant-binding");
            return RemoteHeroActivationResult::Failed;
        }
        lifecyclePhase_ = RemoteHeroLifecyclePhase::NativeReady;
        return RemoteHeroActivationResult::Ready;
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

    void RemoteHeroActor::Suspend(
        const PlayerState& state,
        const std::string& localMap) noexcept
    {
        if (avatar_ == nullptr || avatarSuspended_)
        {
            return;
        }
        if (combatants_ != nullptr)
        {
            combatants_->Unbind(actorId_, nativeAvatar_);
        }
        if (control_ != nullptr)
        {
            // Keep the highest-priority control lease while the network player
            // is in another map. Region-follower membership remains live for
            // the party HUD, but this hidden proxy must not begin autonomous
            // follow or combat behavior in the local map.
            control_->ClearAllActions(true);
            control_->ClearCommands();
        }
        movement_.Detach();
        if (look_ != nullptr)
        {
            look_->StopRouting(avatar_, true);
        }
        bool collidable = false;
        bool drawable = false;
        if (avatar_->IsValid())
        {
            avatar_->SetAttackable(false);
            avatar_->SetDamageable(false);
            collidable = avatar_->SetCollidable(false);
            drawable = avatar_->SetDrawable(false);
        }
        avatarSuspended_ = true;
        lifecyclePhase_ = RemoteHeroLifecyclePhase::BaselineApplied;
        char detail[256] = {};
        std::snprintf(
            detail, sizeof(detail),
            "player=%s remote_map=%s local_map=%s action=hidden-dormant collidable=%s drawable=%s",
            state.playerId.c_str(), state.mapName.c_str(), localMap.c_str(),
            collidable ? "false" : "failed",
            drawable ? "false" : "failed");
        diagnostics_.Event("MultiplayerRemoteAvatarSuspended", detail);
    }

    bool RemoteHeroActor::Resume(
        const PlayerState& state,
        const std::string& localMap,
        game::Entity* localHero,
        std::uint64_t receivedAt)
    {
        if (avatar_ == nullptr || !avatarSuspended_ || !avatar_->IsValid() ||
            nativeAvatar_ == nullptr || npcs_ == nullptr || look_ == nullptr)
        {
            return false;
        }
        if (control_ == nullptr)
        {
            control_ = npcs_->TakeControl(
                avatar_, game::AiPriority::Highest);
        }
        void* const currentHero =
            localHero != nullptr && localHero->IsValid()
                ? entities_->ResolveNative(localHero->NativeHandle())
                : nullptr;
        if (!companionRegistered_ || currentHero != nativeCompanionHero_)
        {
            game::creature::companion::native::CompanionRegistration
                companion;
            if (currentHero == nullptr ||
                !game::creature::companion::native::CompanionFunctions::
                    RegisterWithHero(
                        entities_->GameModule(),
                        nativeAvatar_,
                        currentHero,
                        companion))
            {
                return false;
            }
            nativeCompanionHero_ = currentHero;
            companionRegistered_ = true;
        }
        if (control_ == nullptr || !control_->ClearCommands() ||
            !avatar_->Teleport(state.position, state.facing, false) ||
            !avatar_->SetAttackable(true) ||
            !avatar_->SetDamageable(true) ||
            !avatar_->SetCollidable(true) || !avatar_->SetDrawable(true))
        {
            return false;
        }
        movement_.Bind(
            *avatar_, nativeAvatar_, MovementSample(state, receivedAt),
            localMap);
        if (!look_->RouteReplicatedMovement(
                avatar_, &RemoteHeroActor::ReadMovement, this))
        {
            return false;
        }
        avatarSuspended_ = false;
        if (combatants_ == nullptr ||
            !combatants_->Bind(actorId_, nativeAvatar_))
        {
            return false;
        }
        char detail[256] = {};
        std::snprintf(
            detail, sizeof(detail), "player=%s map=%s action=resumed",
            state.playerId.c_str(), state.mapName.c_str());
        diagnostics_.Event("MultiplayerRemoteAvatarResumed", detail);
        return true;
    }

    void RemoteHeroActor::BeginWorldTransition() noexcept
    {
        DetachForWorldTransition();
    }

    void RemoteHeroActor::DriveMovement()
    {
        if (IsMovementReady() && look_ != nullptr)
        {
            look_->DriveReplicatedMovement(avatar_);
        }
    }

    bool RemoteHeroActor::IsMovementReady() const
    {
        return initialized_ &&
            lifecyclePhase_ != RemoteHeroLifecyclePhase::Constructing &&
            !avatarSuspended_ && avatar_ != nullptr && avatar_->IsValid();
    }

    bool RemoteHeroActor::IsActive() const
    {
        return IsLifecycleActive() && avatar_ != nullptr &&
            !avatarSuspended_ && avatar_->IsValid();
    }

    bool RemoteHeroActor::MatchesLifecycle(
        std::uint32_t actorGeneration,
        std::uint32_t mapEpoch) const noexcept
    {
        return actorGeneration_ == actorGeneration && mapEpoch_ == mapEpoch;
    }

    void RemoteHeroActor::CompleteWorldTransition() noexcept
    {
        if (avatar_ == nullptr)
        {
            return;
        }
        void* const reboundNative = avatar_->IsValid() && entities_ != nullptr
            ? entities_->ResolveNative(avatar_->NativeHandle())
            : nullptr;
        if (reboundNative == nullptr || reboundNative != nativeAvatar_ ||
            !game::creature::native::CreatureFrameFunctions::ValidateCreature(
                entities_->GameModule(), reboundNative))
        {
            diagnostics_.Event(
                "MultiplayerRemoteWorldPresentationExpired",
                "the persistent remote Hero did not survive native world teardown; a fresh destination presentation will be created");
            Retire();
            return;
        }
        nativeCompanionHero_ = nullptr;
        companionRegistered_ = false;
        presentationStateReported_ = false;
        separationReported_ = false;
        nextSpawnAttemptAt_ = 0;
        lifecyclePhase_ = RemoteHeroLifecyclePhase::BaselineApplied;
        diagnostics_.Event(
            "MultiplayerRemoteWorldPresentationPreserved",
            "the same hidden remote Hero Thing survived world teardown and is ready for destination rebinding");
    }

    void RemoteHeroActor::DetachForWorldTransition() noexcept
    {
        if (avatar_ == nullptr)
        {
            return;
        }
        if (combatants_ != nullptr && nativeAvatar_ != nullptr)
        {
            combatants_->Unbind(actorId_, nativeAvatar_);
        }
        if (companionRegistered_ && entities_ != nullptr &&
            nativeAvatar_ != nullptr && nativeCompanionHero_ != nullptr)
        {
            const bool detached =
                game::creature::companion::native::CompanionFunctions::
                    UnregisterFromHero(
                        entities_->GameModule(),
                        nativeAvatar_,
                        nativeCompanionHero_);
            diagnostics_.Event(
                detached
                    ? "MultiplayerRemoteCompanionUnregistered"
                    : "MultiplayerRemoteCompanionUnregisterFailed",
                "reason=world-transition");
        }
        companionRegistered_ = false;
        nativeCompanionHero_ = nullptr;
        movement_.Detach();
        if (look_ != nullptr)
        {
            look_->StopRouting(avatar_, false);
        }
        if (control_ != nullptr)
        {
            control_->ClearAllActions(true);
            control_->ReleaseControl();
            control_->Release();
            control_ = nullptr;
        }
        if (avatar_->IsValid())
        {
            avatar_->SetAttackable(false);
            avatar_->SetDamageable(false);
            avatar_->SetCollidable(false);
            avatar_->SetDrawable(false);
        }
        avatarSuspended_ = true;
        lifecyclePhase_ = RemoteHeroLifecyclePhase::BaselineApplied;
    }

    void RemoteHeroActor::Retire() noexcept
    {
        abilities_.Unbind();
        combat_.Unbind();
        equipment_.Unbind();
        appearance_.Unbind();
        if (combatants_ != nullptr && nativeAvatar_ != nullptr)
        {
            combatants_->Unbind(actorId_, nativeAvatar_);
        }
        if (presentationFactory_ != nullptr)
        {
            presentationFactory_->Cancel(factoryArmToken_);
        }
        factoryArmToken_ = 0;
        if (definitionHook_ != nullptr)
        {
            definitionHook_->Cancel(definitionArmToken_);
        }
        definitionArmToken_ = 0;
        if (companionRegistered_ && entities_ != nullptr &&
            nativeAvatar_ != nullptr && nativeCompanionHero_ != nullptr)
        {
            const bool detached =
                game::creature::companion::native::CompanionFunctions::
                    UnregisterFromHero(
                        entities_->GameModule(),
                        nativeAvatar_,
                        nativeCompanionHero_);
            diagnostics_.Event(
                detached
                    ? "MultiplayerRemoteCompanionUnregistered"
                    : "MultiplayerRemoteCompanionUnregisterFailed",
                "reason=presentation-retired");
        }
        companionRegistered_ = false;
        nativeCompanionHero_ = nullptr;
        movement_.Detach();
        if (look_ != nullptr && avatar_ != nullptr)
        {
            look_->StopRouting(avatar_, true);
        }
        if (control_ != nullptr)
        {
            control_->ClearAllActions(true);
            control_->ReleaseControl();
        }
        if (avatar_ != nullptr && avatar_->IsValid())
        {
            avatar_->SetCollidable(false);
            avatar_->SetDrawable(false);
        }
        if (control_ != nullptr)
        {
            control_->Release();
            control_ = nullptr;
        }
        if (avatar_ != nullptr)
        {
            if (avatar_->IsValid())
            {
                avatar_->RequestDestroy(false);
            }
            avatar_->Release();
            avatar_ = nullptr;
        }
        nativeAvatar_ = nullptr;
        lifecyclePhase_ = RemoteHeroLifecyclePhase::Constructing;
        appearanceBaselineApplied_ = false;
        equipmentBaselineApplied_ = false;
        playerId_.clear();
        appearanceDefinition_.clear();
        presentationStateReported_ = false;
        avatarSuspended_ = false;
        separationReported_ = false;
    }

    void RemoteHeroActor::Shutdown() noexcept
    {
        Retire();
        movement_.Detach();
        abilities_.Shutdown();
        expressions_.Shutdown();
        combat_.Shutdown();
        equipment_.Shutdown();
        appearance_.Shutdown();
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
        initialized_ = false;
    }
}
