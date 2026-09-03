#include "RemoteHeroNativeLifecycle.h"

#include "Game/Creature/Companion/Native/CompanionFunctions.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Creature/Control/ScriptControl.h"
#include "Game/Creature/Look/CreatureLookService.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/HeroPawn/Appearance/Native/HeroMorphComponent.h"
#include "Game/NPC/NpcService.h"
#include "Multiplayer/Combat/PlayerCombatantDirectory.h"
#include "RemoteHeroVisibilityPolicy.h"

#include <Windows.h>

#include <cstdio>

namespace fable::game::hero_pawn::remote
{
    bool RemoteHeroNativeLifecycle::Spawn(const PlayerState& state)
    {
        Retire();
        owner_.nextSpawnAttemptAt_ = GetTickCount64() + 5'000;
        owner_.definitionArmToken_ = owner_.definitionHook_->Arm();
        if (owner_.definitionArmToken_ == 0)
        {
            owner_.diagnostics_.Event(
                "ClientFailed",
                "multiplayer-remote-runtime-definition-arm");
            return false;
        }
        owner_.factoryArmToken_ = owner_.presentationFactory_->Arm(state.position);
        owner_.avatar_ = owner_.npcs_->Spawn(
            state.appearanceDefinition, state.position,
            "SCRIPT_NAME_ALBION_TOGETHER_REMOTE_PLAYER");
        owner_.definitionHook_->Cancel(owner_.definitionArmToken_);
        owner_.definitionArmToken_ = 0;
        if (owner_.avatar_ == nullptr || !owner_.avatar_->IsValid())
        {
            owner_.diagnostics_.Event(
                "MultiplayerRemoteAvatarSpawnDeferred",
                "native destination construction is still settling; retrying the same actor baseline");
            Retire();
            return false;
        }
        if (owner_.avatar_->GetDefinitionName() != state.appearanceDefinition)
        {
            owner_.diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-avatar-spawn");
            Retire();
            return false;
        }
        owner_.nativeAvatar_ = owner_.entities_->ResolveNative(owner_.avatar_->NativeHandle());
        if (!game::creature::native::CreatureFrameFunctions::ValidateCreature(
                owner_.entities_->GameModule(), owner_.nativeAvatar_))
        {
            owner_.diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-native-type");
            Retire();
            return false;
        }
        game::hero_pawn::appearance::native::HeroMorphResolutionState pending;
        const bool presentationAlreadyReady =
            game::hero_pawn::appearance::native::HeroMorphComponent::
                InspectResolution(owner_.nativeAvatar_, pending);
        (void)presentationAlreadyReady;
        if (pending.graphic != nullptr)
        {
            owner_.presentationFactory_->TargetGraphic(
                owner_.factoryArmToken_, pending.graphic);
        }
        owner_.appearance_.Bind(owner_.nativeAvatar_, owner_.actorId_);
        owner_.nativePresenceObserved_ = false;
        if (game::entity::native::ThingComponentAccess::Has(
                owner_.nativeAvatar_,
                game::entity::native::ThingComponentType::HeroMorph) &&
            !game::hero_pawn::appearance::native::HeroMorphComponent::
                SetUpdateRequested(owner_.nativeAvatar_, false))
        {
            owner_.diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-hero-morph-suppression");
            Retire();
            return false;
        }
        const bool appearanceReady = state.heroMorph.IsSane() &&
            state.heroClothing.IsSane() &&
            state.heroBoneScales.IsSane() &&
            state.heroAppearanceModifiers.IsSane();
        if (!owner_.appearance_.StageInitial(state.heroMorph, appearanceReady))
        {
            owner_.diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-morph-values");
            Retire();
            return false;
        }
        owner_.playerId_ = state.playerId;
        owner_.appearanceDefinition_ = state.appearanceDefinition;
        // Keep distance/visibility culling from removing a live remote Hero.
        // World departure explicitly retires this native body through the
        // game's destruction path; persistence is not cross-world reuse.
        ApplyActivePresentationFlags(true);
        owner_.lifecyclePhase_ = RemoteHeroLifecyclePhase::Constructing;
        owner_.nextSpawnAttemptAt_ = 0;

        char detail[384] = {};
        std::snprintf(
            detail, sizeof(detail),
            "player=%s actor_id=%llu authority_epoch=%u definition=%s exact=true map=%s state=awaiting-native-presentation",
            owner_.playerId_.c_str(),
            static_cast<unsigned long long>(state.actorId),
            state.authorityEpoch, owner_.appearanceDefinition_.c_str(),
            state.mapName.c_str());
        owner_.diagnostics_.Event("MultiplayerRemoteDefinitionCreated", detail);
        return true;
    }

    RemoteHeroActivationResult RemoteHeroNativeLifecycle::Activate(
        const PlayerState& state,
        const std::string& localMap,
        game::Entity* localHero,
        std::uint64_t receivedAt)
    {
        game::hero_pawn::appearance::native::HeroMorphResolutionState
            presentation;
        if (!game::hero_pawn::appearance::native::HeroMorphComponent::
                InspectResolution(owner_.nativeAvatar_, presentation))
        {
            return RemoteHeroActivationResult::Pending;
        }
        // Hero-only components are part of the private runtime definition, so
        // these binds validate an already-complete graph. They must not add
        // components while Fable is still constructing the skeletal pawn.
        if (!owner_.equipment_.Bind(*owner_.avatar_, owner_.nativeAvatar_, owner_.actorId_))
        {
            owner_.diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-hero-equipment-bind");
            return RemoteHeroActivationResult::Failed;
        }
        if (!owner_.abilities_.Bind(owner_.nativeAvatar_, owner_.actorId_))
        {
            owner_.diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-hero-ability-bind");
            return RemoteHeroActivationResult::Failed;
        }
        if (!owner_.combat_.Bind(*owner_.avatar_, owner_.nativeAvatar_, owner_.actorId_))
        {
            owner_.diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-health-authority-fence");
            return RemoteHeroActivationResult::Failed;
        }
        owner_.nativeCompanionHero_ = localHero != nullptr && localHero->IsValid()
            ? owner_.entities_->ResolveNative(localHero->NativeHandle())
            : nullptr;
        game::creature::companion::native::CompanionRegistration companion;
        if (owner_.nativeCompanionHero_ == nullptr ||
            !game::creature::companion::native::CompanionFunctions::
                RegisterWithHero(
                    owner_.entities_->GameModule(),
                    owner_.nativeAvatar_,
                    owner_.nativeCompanionHero_,
                    companion))
        {
            char detail[256] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "actor_id=%llu avatar=%p hero=%p enemy=%p region_follower=%p",
                static_cast<unsigned long long>(owner_.actorId_),
                owner_.nativeAvatar_,
                owner_.nativeCompanionHero_,
                companion.followerEnemy,
                companion.heroRegionFollower);
            owner_.diagnostics_.Event(
                "MultiplayerRemoteCompanionRegistrationFailed", detail);
            return RemoteHeroActivationResult::Failed;
        }
        owner_.companionRegistered_ = true;
        char companionDetail[320] = {};
        std::snprintf(
            companionDetail,
            sizeof(companionDetail),
            "actor_id=%llu avatar=%p hero=%p enemy=%p region_follower=%p count_before=%u count_after=%u existing=%s locomotion=replicated",
            static_cast<unsigned long long>(owner_.actorId_),
            owner_.nativeAvatar_,
            owner_.nativeCompanionHero_,
            companion.followerEnemy,
            companion.heroRegionFollower,
            companion.followerCountBefore,
            companion.followerCountAfter,
            companion.alreadyRegistered ? "true" : "false");
        owner_.diagnostics_.Event(
            "MultiplayerRemoteCompanionRegistered", companionDetail);
        owner_.control_ = owner_.npcs_->TakeControl(owner_.avatar_, game::AiPriority::Highest);
        // Promoting the AI pawn through the retail Hero presentation can
        // enqueue a template weapon action while its components initialize.
        // No replicated action is admissible before this actor becomes
        // Active, so clear that bootstrap stack before applying the reliable
        // equipment baseline.
        if (owner_.control_ == nullptr || !owner_.control_->ClearAllActions(true) ||
            !owner_.control_->ClearCommands())
        {
            owner_.diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-avatar-control");
            return RemoteHeroActivationResult::Failed;
        }
        owner_.movement_.Bind(
            *owner_.avatar_, owner_.nativeAvatar_, RemoteHeroActor::MovementSample(
                state, receivedAt), localMap);
        if (!owner_.look_->RouteReplicatedMovement(
                owner_.avatar_, &RemoteHeroActor::ReadMovement, &owner_))
        {
            owner_.diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-movement-routing");
            return RemoteHeroActivationResult::Failed;
        }

        char detail[384] = {};
        std::snprintf(
            detail, sizeof(detail),
            "player=%s actor_id=%llu authority_epoch=%u definition=%s exact=true map=%s control=scripted",
            owner_.playerId_.c_str(),
            static_cast<unsigned long long>(state.actorId),
            state.authorityEpoch, owner_.appearanceDefinition_.c_str(),
            state.mapName.c_str());
        owner_.diagnostics_.Event("MultiplayerRemoteAvatarReady", detail);
        owner_.diagnostics_.Event(
            "MultiplayerPresentationChannelOpened",
            "remote native creature presentation is scoped to the shared map lifecycle");
        if (!owner_.combatants_->Bind(owner_.actorId_, owner_.nativeAvatar_))
        {
            owner_.diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-combatant-binding");
            return RemoteHeroActivationResult::Failed;
        }
        owner_.lifecyclePhase_ = RemoteHeroLifecyclePhase::NativeReady;
        return RemoteHeroActivationResult::Ready;
    }

    void RemoteHeroNativeLifecycle::ApplyActivePresentationFlags(
        const bool configureUnloadPolicy) noexcept
    {
        if (owner_.avatar_ == nullptr || !owner_.avatar_->IsValid())
        {
            return;
        }

        const bool unload = !configureUnloadPolicy ||
            owner_.avatar_->SetKillOnLevelUnload(false);
        const bool persistent = owner_.avatar_->SetPersistent(true);
        const bool attackable = owner_.avatar_->SetAttackable(true);
        const bool damageable = owner_.avatar_->SetDamageable(true);
        const bool friendly = owner_.avatar_->SetFriendsWithEverything(false);
        const bool collidable = owner_.avatar_->SetCollidable(true);
        const bool drawable = owner_.avatar_->SetDrawable(true);
        owner_.lastPresentationRepairAt_ = 0;

        if (unload && persistent && attackable && damageable && friendly &&
            collidable && drawable)
        {
            return;
        }

        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "unload=%s persistent=%s attackable=%s damageable=%s friendly=%s collidable=%s drawable=%s; presentation remains active and retries persistence and visibility state",
            unload ? "ok" : "failed",
            persistent ? "ok" : "failed",
            attackable ? "ok" : "failed",
            damageable ? "ok" : "failed",
            friendly ? "ok" : "failed",
            collidable ? "ok" : "failed",
            drawable ? "ok" : "failed");
        owner_.diagnostics_.Event(
            "MultiplayerRemotePresentationFlagsPartial",
            detail);
    }

    void RemoteHeroNativeLifecycle::DriveMovement()
    {
        if (owner_.IsMovementReady() && owner_.look_ != nullptr)
        {
            const std::uint64_t now = GetTickCount64();
            if (owner_.avatar_ != nullptr && owner_.avatar_->IsValid() &&
                IsActivePresentationRepairDue(
                    now, owner_.lastPresentationRepairAt_))
            {
                owner_.lastPresentationRepairAt_ = now;
                // Fable may rewrite presentation flags while an actor is far
                // from the camera or while level streaming settles. Remote
                // Heroes are network actors, not ambient population: keep
                // their native Thing alive and drawable until the replicated
                // lifecycle explicitly suspends or retires it.
                const bool unloadPolicyRepaired =
                    owner_.avatar_->SetKillOnLevelUnload(false);
                const bool persistenceRepaired =
                    owner_.avatar_->SetPersistent(true);
                const bool drawableRepaired =
                    owner_.avatar_->SetDrawable(true);
                const bool repaired = unloadPolicyRepaired &&
                    persistenceRepaired && drawableRepaired;
                if (!repaired &&
                    !owner_.presentationRepairFailureReported_)
                {
                    owner_.presentationRepairFailureReported_ = true;
                    char detail[192] = {};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "unload=%s persistent=%s drawable=%s",
                        unloadPolicyRepaired ? "ok" : "failed",
                        persistenceRepaired ? "ok" : "failed",
                        drawableRepaired ? "ok" : "failed");
                    owner_.diagnostics_.Event(
                        "MultiplayerRemoteAvatarPersistenceRepairFailed",
                        detail);
                }
                else if (repaired)
                {
                    owner_.presentationRepairFailureReported_ = false;
                }
            }
            owner_.look_->DriveReplicatedMovement(owner_.avatar_);
        }
    }

    void RemoteHeroNativeLifecycle::Retire() noexcept
    {
        // Close action/movement admission before any native unbind callback
        // can re-enter the actor while its component graph is being removed.
        owner_.lifecyclePhase_ = RemoteHeroLifecyclePhase::Constructing;
        owner_.appearanceBaselineApplied_ = false;
        owner_.equipmentBaselineApplied_ = false;
        owner_.abilities_.Unbind();
        owner_.combat_.Unbind();
        owner_.equipment_.Unbind();
        owner_.appearance_.Unbind();
        if (owner_.combatants_ != nullptr && owner_.nativeAvatar_ != nullptr)
        {
            owner_.combatants_->Unbind(owner_.actorId_, owner_.nativeAvatar_);
        }
        if (owner_.presentationFactory_ != nullptr)
        {
            owner_.presentationFactory_->Cancel(owner_.factoryArmToken_);
        }
        owner_.factoryArmToken_ = 0;
        if (owner_.definitionHook_ != nullptr)
        {
            owner_.definitionHook_->Cancel(owner_.definitionArmToken_);
        }
        owner_.definitionArmToken_ = 0;
        if (owner_.companionRegistered_ && owner_.entities_ != nullptr &&
            owner_.nativeAvatar_ != nullptr && owner_.nativeCompanionHero_ != nullptr)
        {
            const bool detached =
                game::creature::companion::native::CompanionFunctions::
                    UnregisterFromHero(
                        owner_.entities_->GameModule(),
                        owner_.nativeAvatar_,
                        owner_.nativeCompanionHero_);
            owner_.diagnostics_.Event(
                detached
                    ? "MultiplayerRemoteCompanionUnregistered"
                    : "MultiplayerRemoteCompanionUnregisterFailed",
                "reason=presentation-retired");
        }
        owner_.companionRegistered_ = false;
        owner_.nativeCompanionHero_ = nullptr;
        owner_.movement_.Detach();
        if (owner_.look_ != nullptr && owner_.avatar_ != nullptr)
        {
            owner_.look_->StopRouting(owner_.avatar_, true);
        }
        if (owner_.control_ != nullptr)
        {
            owner_.control_->ClearAllActions(true);
            owner_.control_->ReleaseControl();
        }
        if (owner_.avatar_ != nullptr && owner_.avatar_->IsValid())
        {
            owner_.avatar_->SetAttackable(false);
            owner_.avatar_->SetDamageable(false);
            owner_.avatar_->SetCollidable(false);
            owner_.avatar_->SetDrawable(false);
        }
        if (owner_.control_ != nullptr)
        {
            owner_.control_->Release();
            owner_.control_ = nullptr;
        }
        if (owner_.avatar_ != nullptr)
        {
            if (owner_.avatar_->IsValid())
            {
                // Remote Heroes are persistent only in their local world
                // incarnation. Demote the presentation before
                // asking the game to destroy the actor; otherwise a
                // persistent Thing can survive its presentation owner and be
                // touched by the next world teardown.
                owner_.avatar_->SetPersistent(false);
                owner_.avatar_->SetKillOnLevelUnload(true);
                owner_.avatar_->RequestDestroy(false);
            }
            owner_.avatar_->Release();
            owner_.avatar_ = nullptr;
        }
        owner_.nativeAvatar_ = nullptr;
        owner_.nativePresenceObserved_ = false;
        owner_.lastPresentationRepairAt_ = 0;
        owner_.presentationRepairFailureReported_ = false;
        owner_.playerId_.clear();
        owner_.appearanceDefinition_.clear();
        owner_.presentationStateReported_ = false;
        owner_.separationReported_ = false;
    }
}
