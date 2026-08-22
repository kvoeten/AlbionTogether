#include "RemoteHeroActor.h"

#include "Game/Creature/Companion/Native/CompanionFunctions.h"
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
        game::creature::combat::CreatureCombatService& combat,
        game::hero_pawn::abilities::HeroWillAbilityService& abilities,
        multiplayer::combat::PlayerCombatantDirectory& combatants,
        const core::Diagnostics& diagnostics,
        game::hero_pawn::appearance::hooks::
            RemoteHeroPresentationFactoryHook& presentationFactory)
    {
        Shutdown();
        entities_ = &entities;
        npcs_ = &npcs;
        look_ = &look;
        combatants_ = &combatants;
        diagnostics_ = diagnostics;
        presentationFactory_ = &presentationFactory;
        movement_.Initialize(locomotion, diagnostics);
        appearance_.Initialize(diagnostics);
        equipment_.Initialize(entities, diagnostics);
        combat_.Initialize(entities, combat, equipment_, diagnostics);
        abilities_.Initialize(entities, abilities, diagnostics);
        initialized_ = true;
        return true;
    }

    bool RemoteHeroActor::ApplyHealth(
        float currentHealth,
        float maximumHealth,
        std::uint32_t revision)
    {
        return initialized_ && !avatarSuspended_ &&
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
        return initialized_ && !avatarSuspended_ &&
            combat_.PerformAbility(
                weaponFamily,
                requiredWeapons,
                meleeAttachmentSlot,
                rangedAttachmentSlot,
                abilityId,
                charge,
                targetCreature, resolvedActionType, resolvedAnimationId);
    }

    bool RemoteHeroActor::PerformWeaponTransition(
        game::creature::equipment::CreatureWeaponFamily weaponFamily,
        const game::hero_pawn::equipment::HeroWeaponDefinitions&
            requiredWeapons,
        std::uint32_t meleeAttachmentSlot,
        std::uint32_t rangedAttachmentSlot,
        const std::string& resolvedActionType,
        std::uint32_t resolvedAnimationId)
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
        return initialized_ && !avatarSuspended_ &&
            equipment_.PerformTransition(
                equipment, resolvedActionType, resolvedAnimationId);
    }

    bool RemoteHeroActor::PerformHeroAbility(
        game::hero_pawn::abilities::HeroAbility ability,
        game::hero_pawn::abilities::HeroAbilityCommand command,
        std::int32_t progressionState,
        void* targetCreature)
    {
        return initialized_ && !avatarSuspended_ &&
            abilities_.Perform(
                ability, command, progressionState, targetCreature);
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
        sample.receivedAt = receivedAt;
        return sample;
    }

    void RemoteHeroActor::Reconcile(
        const PlayerState& state,
        const std::string& localMap,
        game::Entity* localHero,
        std::uint64_t receivedAt)
    {
        if (!initialized_ || state.actorId == 0 || state.playerId.empty() ||
            state.appearanceDefinition.empty())
        {
            return;
        }
        actorId_ = state.actorId;
        movement_.Update(MovementSample(state, receivedAt), localMap);
        if (state.mapName.empty() || state.mapName != localMap)
        {
            Suspend(state, localMap);
            return;
        }
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
            if (!Spawn(state, localMap, localHero, receivedAt))
            {
                return;
            }
        }

        const bool appearanceReady = state.heroMorph.IsSane() &&
            state.heroClothing.IsSane() &&
            state.heroBoneScales.IsSane() &&
            state.heroAppearanceModifiers.IsSane();
        if (!presentationStateReported_ &&
            (appearanceReady || state.heroEquipment.IsSane()))
        {
            presentationStateReported_ = true;
            char detail[224] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "actor_id=%llu appearance=%s equipment=%s melee=%d ranged=%d",
                static_cast<unsigned long long>(actorId_),
                appearanceReady ? "ready" : "pending",
                state.heroEquipment.IsSane() ? "ready" : "pending",
                state.heroEquipment.meleeDefinitionIndex,
                state.heroEquipment.rangedDefinitionIndex);
            diagnostics_.Event(
                "MultiplayerRemotePresentationStateReceived", detail);
        }

        const bool presentationRequired =
            appearanceReady || state.heroEquipment.IsSane();
        const game::hero_pawn::appearance::RemoteHeroAppearanceResult
            appearanceResult = appearance_.Reconcile(
                state.heroMorph,
                state.heroClothing,
                state.heroBoneScales,
                state.heroAppearanceModifiers,
                presentationRequired);
        if (appearanceResult == game::hero_pawn::appearance::
                RemoteHeroAppearanceResult::Pending)
        {
            return;
        }
        if (appearanceResult == game::hero_pawn::appearance::
                RemoteHeroAppearanceResult::Failed)
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-appearance-refresh");
            Retire();
            return;
        }

        equipment_.Reconcile(state.heroEquipment, now);
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

    bool RemoteHeroActor::Spawn(
        const PlayerState& state,
        const std::string& localMap,
        game::Entity* localHero,
        std::uint64_t receivedAt)
    {
        Retire();
        nextSpawnAttemptAt_ = GetTickCount64() + 5'000;
        factoryArmToken_ = presentationFactory_->Arm(state.position);
        avatar_ = npcs_->Spawn(
            state.appearanceDefinition, state.position,
            "SCRIPT_NAME_FABLE_TOGETHER_REMOTE_PLAYER");
        if (avatar_ == nullptr || !avatar_->IsValid() ||
            avatar_->GetDefinitionName() != state.appearanceDefinition)
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
        // Complete the promoted Hero's native component graph before binding
        // or staging any appearance resources. Adding an inventory component
        // can rebuild the Hero graphic; doing so afterward invalidates the
        // staged morph target and creates a presentation respawn loop.
        if (!equipment_.Bind(*avatar_, nativeAvatar_, actorId_))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-hero-equipment-bind");
            Retire();
            return false;
        }
        if (!abilities_.Bind(nativeAvatar_, actorId_))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-hero-ability-bind");
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
        if (!combat_.Bind(*avatar_, nativeAvatar_, actorId_))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-health-authority-fence");
            Retire();
            return false;
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
            Retire();
            return false;
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
            Retire();
            return false;
        }
        movement_.Bind(
            *avatar_, nativeAvatar_, MovementSample(state, receivedAt),
            localMap);
        if (!look_->RouteReplicatedMovement(
                avatar_, &RemoteHeroActor::ReadMovement, this))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-movement-routing");
            Retire();
            return false;
        }

        char detail[384] = {};
        std::snprintf(
            detail, sizeof(detail),
            "player=%s actor_id=%llu authority_epoch=%u definition=%s exact=true map=%s control=scripted",
            playerId_.c_str(),
            static_cast<unsigned long long>(state.actorId),
            state.authorityEpoch, appearanceDefinition_.c_str(),
            state.mapName.c_str());
        diagnostics_.Event("MultiplayerRemoteDefinitionCreated", detail);
        diagnostics_.Event("MultiplayerRemoteAvatarReady", detail);
        diagnostics_.Event(
            "MultiplayerPresentationChannelOpened",
            "remote native creature presentation is scoped to the shared map lifecycle");
        if (!combatants_->Bind(actorId_, nativeAvatar_))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-combatant-binding");
            Retire();
            return false;
        }
        nextSpawnAttemptAt_ = 0;
        return true;
    }

    bool RemoteHeroActor::ReadMovement(
        void* context,
        void* creature,
        movement::ReplicatedActorMovement::NativeInput& input)
    {
        auto* const presentation =
            static_cast<RemoteHeroActor*>(context);
        return presentation != nullptr &&
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
        Retire(true);
    }

    void RemoteHeroActor::DriveMovement()
    {
        if (initialized_ && !avatarSuspended_ && avatar_ != nullptr &&
            avatar_->IsValid() && look_ != nullptr)
        {
            look_->DriveReplicatedMovement(avatar_);
        }
    }

    bool RemoteHeroActor::IsActive() const
    {
        return avatar_ != nullptr && !avatarSuspended_ && avatar_->IsValid();
    }

    void RemoteHeroActor::CompleteWorldTransition() noexcept
    {
        // Anything already quarantined has survived a complete world
        // generation. Retire it before retaining this transition's actor.
        ReapQuarantinedAvatars();
        if (deferred_.control != nullptr)
        {
            deferred_.control->Release();
            deferred_.control = nullptr;
        }
        if (deferred_.avatar != nullptr && deferred_.avatar->IsValid())
        {
            char detail[256] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "player=%s native=%p state=hidden-quarantined destination=respawn",
                playerId_.empty() ? "<remote>" : playerId_.c_str(),
                deferred_.nativeAvatar);
            diagnostics_.Event(
                "MultiplayerRemoteWorldPresentationQuarantined",
                detail);
            quarantinedAvatars_.push_back(deferred_.avatar);
        }
        else if (deferred_.avatar != nullptr)
        {
            deferred_.avatar->Release();
        }
        deferred_.avatar = nullptr;
        deferred_.nativeAvatar = nullptr;
        nativeCompanionHero_ = nullptr;
        companionRegistered_ = false;
        playerId_.clear();
        appearanceDefinition_.clear();
        presentationStateReported_ = false;
        separationReported_ = false;
        avatarSuspended_ = false;
        nextSpawnAttemptAt_ = 0;
    }

    void RemoteHeroActor::ReapQuarantinedAvatars() noexcept
    {
        for (game::Entity* avatar : quarantinedAvatars_)
        {
            if (avatar == nullptr)
            {
                continue;
            }
            if (avatar->IsValid())
            {
                avatar->RequestDestroy(false);
            }
            avatar->Release();
        }
        quarantinedAvatars_.clear();
    }

    void RemoteHeroActor::Retire(bool worldUnloading) noexcept
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
                worldUnloading
                    ? "reason=world-transition"
                    : "reason=presentation-retired");
        }
        companionRegistered_ = false;
        nativeCompanionHero_ = nullptr;
        movement_.Detach();
        if (look_ != nullptr && avatar_ != nullptr)
        {
            look_->StopRouting(avatar_, !worldUnloading);
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
        if (worldUnloading && (avatar_ != nullptr || control_ != nullptr))
        {
            deferred_ = {avatar_, nativeAvatar_, control_};
            avatar_ = nullptr;
            nativeAvatar_ = nullptr;
            control_ = nullptr;
        }
        if (control_ != nullptr)
        {
            control_->Release();
            control_ = nullptr;
        }
        if (avatar_ != nullptr)
        {
            if (!worldUnloading && avatar_->IsValid())
            {
                avatar_->RequestDestroy(false);
            }
            avatar_->Release();
            avatar_ = nullptr;
        }
        nativeAvatar_ = nullptr;
        if (!worldUnloading)
        {
            playerId_.clear();
            appearanceDefinition_.clear();
            presentationStateReported_ = false;
            avatarSuspended_ = false;
            separationReported_ = false;
        }
    }

    void RemoteHeroActor::Shutdown() noexcept
    {
        Retire();
        CompleteWorldTransition();
        ReapQuarantinedAvatars();
        movement_.Detach();
        abilities_.Shutdown();
        combat_.Shutdown();
        equipment_.Shutdown();
        appearance_.Shutdown();
        entities_ = nullptr;
        npcs_ = nullptr;
        look_ = nullptr;
        combatants_ = nullptr;
        presentationFactory_ = nullptr;
        diagnostics_ = {};
        nextSpawnAttemptAt_ = 0;
        actorId_ = 0;
        initialized_ = false;
    }
}
