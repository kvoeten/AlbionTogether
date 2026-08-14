#include "RemotePlayerPresentation.h"

#include "Game/Creature/Control/ScriptControl.h"
#include "Game/Creature/Look/CreatureLookService.h"
#include "Game/Creature/Locomotion/CreatureLocomotionService.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/HeroPawn/Appearance/Native/HeroAttachableAppearanceComponent.h"
#include "Game/HeroPawn/Appearance/Native/HeroClothingComponent.h"
#include "Game/HeroPawn/Appearance/Native/HeroMorphComponent.h"
#include "Game/NPC/NpcService.h"
#include "Multiplayer/Presentation/HeroPresentationDiagnostics.h"

#include <Windows.h>

#include <cstdio>

namespace
{
    constexpr float kMinimumVisiblePlayerSeparation = 1.25f;
}

namespace fable::multiplayer::presentation
{
    bool RemotePlayerPresentation::Initialize(
        game::EntityService& entities,
        game::NpcService& npcs,
        game::creature::locomotion::CreatureLocomotionService& locomotion,
        game::creature::look::CreatureLookService& look,
        const core::Diagnostics& diagnostics,
        game::hero_pawn::appearance::hooks::
            RemoteHeroPresentationFactoryHook& presentationFactory)
    {
        Shutdown();
        entities_ = &entities;
        npcs_ = &npcs;
        look_ = &look;
        diagnostics_ = diagnostics;
        presentationFactory_ = &presentationFactory;
        movement_.Initialize(locomotion, diagnostics);
        initialized_ = true;
        return true;
    }

    movement::ReplicatedMovementSample
        RemotePlayerPresentation::MovementSample(
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

    void RemotePlayerPresentation::Reconcile(
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
        movement_.Update(MovementSample(state, receivedAt), localMap);
        if (state.mapName.empty() || state.mapName != localMap)
        {
            Suspend(state, localMap);
            return;
        }
        if (avatarSuspended_ && !Resume(state, localMap, receivedAt))
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
            if (!Spawn(state, localMap, receivedAt))
            {
                return;
            }
        }

        const bool appearanceReady = state.heroMorph.IsSane() &&
            state.heroClothing.IsSane() &&
            state.heroBoneScales.IsSane() &&
            state.heroAppearanceModifiers.IsSane();
        game::hero_pawn::appearance::native::HeroMorphResolutionState
            resolution;
        if (appearanceReady &&
            !game::hero_pawn::appearance::native::HeroMorphComponent::
                InspectResolution(nativeAvatar_, resolution))
        {
            return;
        }
        if (!graphicRuntimeReported_)
        {
            graphicRuntimeReported_ = ReportHeroSkeletalPresentation(
                "remote", nativeAvatar_, diagnostics_);
        }
        if (appearanceReady && !appliedClothing_.Equals(state.heroClothing))
        {
            std::uint32_t inserted = 0;
            if (!game::hero_pawn::appearance::native::HeroClothingComponent::Apply(
                    nativeAvatar_, state.heroClothing, &inserted))
            {
                return;
            }
            appliedClothing_ = state.heroClothing;
            char detail[224] = {};
            std::snprintf(
                detail, sizeof(detail),
                "selected=(%d,%d,%d,%d,%d,%d) inserted=%u operation=native-wear-rebuild",
                state.heroClothing.definitionIndices[0],
                state.heroClothing.definitionIndices[1],
                state.heroClothing.definitionIndices[2],
                state.heroClothing.definitionIndices[3],
                state.heroClothing.definitionIndices[4],
                state.heroClothing.definitionIndices[5], inserted);
            diagnostics_.Event("MultiplayerRemoteClothingApplied", detail);
        }
        if (appearanceReady &&
            !appliedModifiers_.Equals(state.heroAppearanceModifiers))
        {
            std::uint32_t removed = 0;
            std::uint32_t added = 0;
            if (!game::hero_pawn::appearance::native::
                    HeroAttachableAppearanceComponent::Apply(
                        nativeAvatar_, state.heroAppearanceModifiers,
                        &removed, &added))
            {
                return;
            }
            appliedModifiers_ = state.heroAppearanceModifiers;
            char detail[192] = {};
            std::snprintf(
                detail, sizeof(detail),
                "source=%u removed=%u added=%u operation=component-refresh",
                state.heroAppearanceModifiers.count, removed, added);
            diagnostics_.Event(
                "MultiplayerRemoteAppearanceModifiersApplied", detail);
        }
        if (appearanceReady && !appliedMorph_.Equals(state.heroMorph))
        {
            if (!game::hero_pawn::appearance::native::HeroMorphComponent::
                    ApplyValues(nativeAvatar_, state.heroMorph))
            {
                diagnostics_.Event(
                    "ClientFailed", "multiplayer-remote-morph-refresh");
                Retire();
                return;
            }
            appliedMorph_ = state.heroMorph;
        }
        if (appearanceReady &&
            !appliedBoneScales_.Equals(state.heroBoneScales))
        {
            std::uint32_t matched = 0;
            if (!game::hero_pawn::appearance::native::HeroMorphComponent::
                    ApplyBoneScaleState(
                        nativeAvatar_, state.heroBoneScales, &matched))
            {
                diagnostics_.Event(
                    "ClientFailed", "multiplayer-remote-bone-scale-refresh");
                Retire();
                return;
            }
            appliedBoneScales_ = state.heroBoneScales;
            char detail[160] = {};
            std::snprintf(
                detail, sizeof(detail),
                "matched=%u source=%u operation=refresh",
                matched, state.heroBoneScales.count);
            diagnostics_.Event("MultiplayerRemoteBoneScalesApplied", detail);
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

    bool RemotePlayerPresentation::Spawn(
        const PlayerState& state,
        const std::string& localMap,
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
        if (!game::creature::native::CreatureFrameFunctions::ValidateCreature(
                entities_->GameModule(), nativeAvatar_))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-native-type");
            Retire();
            return false;
        }
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
        if (appearanceReady &&
            !game::hero_pawn::appearance::native::HeroMorphComponent::
                ApplyValues(nativeAvatar_, state.heroMorph))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-morph-values");
            Retire();
            return false;
        }
        if (appearanceReady)
        {
            appliedMorph_ = state.heroMorph;
            diagnostics_.Event(
                "MultiplayerRemoteAppearancePending",
                "remote Hero-compatible creature scalar appearance staged; waiting for its UE3 skeletal presentation and MassBoneScaling control");
        }
        else
        {
            diagnostics_.Event(
                "MultiplayerRemoteAppearancePending",
                "remote Hero-compatible creature constructed with default presentation while morph translation is pending");
        }
        playerId_ = state.playerId;
        appearanceDefinition_ = state.appearanceDefinition;
        // A promoted Hero presentation owns asynchronous composite-texture and
        // bone-scaling work. Level-unload destruction can free those
        // components before the graphics queue consumes them, so keep it alive
        // through teardown. It is quarantined after unload; the destination
        // receives a fresh map-scoped presentation.
        if (!avatar_->SetKillOnLevelUnload(false) ||
            !avatar_->SetAttackable(false) ||
            !avatar_->SetDamageable(false) ||
            !avatar_->SetFriendsWithEverything(true) ||
            !avatar_->SetCollidable(true) || !avatar_->SetDrawable(true))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-remote-presentation-flags");
            Retire();
            return false;
        }
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
                avatar_, &RemotePlayerPresentation::ReadMovement, this))
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
        nextSpawnAttemptAt_ = 0;
        return true;
    }

    bool RemotePlayerPresentation::ReadMovement(
        void* context,
        void* creature,
        movement::ReplicatedActorMovement::NativeInput& input)
    {
        auto* const presentation =
            static_cast<RemotePlayerPresentation*>(context);
        return presentation != nullptr &&
            presentation->movement_.Provide(creature, input);
    }

    void RemotePlayerPresentation::Suspend(
        const PlayerState& state,
        const std::string& localMap) noexcept
    {
        if (avatar_ == nullptr || avatarSuspended_)
        {
            return;
        }
        if (control_ != nullptr)
        {
            control_->ClearAllActions(true);
            control_->ReleaseControl();
            control_->Release();
            control_ = nullptr;
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

    bool RemotePlayerPresentation::Resume(
        const PlayerState& state,
        const std::string& localMap,
        std::uint64_t receivedAt)
    {
        if (avatar_ == nullptr || !avatarSuspended_ || !avatar_->IsValid() ||
            nativeAvatar_ == nullptr || npcs_ == nullptr || look_ == nullptr)
        {
            return false;
        }
        control_ = npcs_->TakeControl(avatar_, game::AiPriority::Highest);
        if (control_ == nullptr || !control_->ClearCommands() ||
            !avatar_->Teleport(state.position, state.facing, false) ||
            !avatar_->SetCollidable(true) || !avatar_->SetDrawable(true))
        {
            return false;
        }
        movement_.Bind(
            *avatar_, nativeAvatar_, MovementSample(state, receivedAt),
            localMap);
        if (!look_->RouteReplicatedMovement(
                avatar_, &RemotePlayerPresentation::ReadMovement, this))
        {
            return false;
        }
        avatarSuspended_ = false;
        char detail[256] = {};
        std::snprintf(
            detail, sizeof(detail), "player=%s map=%s action=resumed",
            state.playerId.c_str(), state.mapName.c_str());
        diagnostics_.Event("MultiplayerRemoteAvatarResumed", detail);
        return true;
    }

    void RemotePlayerPresentation::BeginWorldTransition() noexcept
    {
        Retire(true);
    }

    void RemotePlayerPresentation::DriveMovement()
    {
        if (initialized_ && !avatarSuspended_ && avatar_ != nullptr &&
            avatar_->IsValid() && look_ != nullptr)
        {
            look_->DriveReplicatedMovement(avatar_);
        }
    }

    bool RemotePlayerPresentation::IsActive() const
    {
        return avatar_ != nullptr && !avatarSuspended_ && avatar_->IsValid();
    }

    void RemotePlayerPresentation::CompleteWorldTransition() noexcept
    {
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
        playerId_.clear();
        appearanceDefinition_.clear();
        appliedMorph_ = {};
        appliedClothing_ = {};
        appliedBoneScales_ = {};
        appliedModifiers_ = {};
        graphicRuntimeReported_ = false;
        separationReported_ = false;
        avatarSuspended_ = false;
        nextSpawnAttemptAt_ = 0;
    }

    void RemotePlayerPresentation::Retire(bool worldUnloading) noexcept
    {
        if (presentationFactory_ != nullptr)
        {
            presentationFactory_->Cancel(factoryArmToken_);
        }
        factoryArmToken_ = 0;
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
            appliedMorph_ = {};
            appliedClothing_ = {};
            appliedBoneScales_ = {};
            appliedModifiers_ = {};
            graphicRuntimeReported_ = false;
            avatarSuspended_ = false;
            separationReported_ = false;
        }
    }

    void RemotePlayerPresentation::Shutdown() noexcept
    {
        Retire();
        CompleteWorldTransition();
        for (game::Entity* avatar : quarantinedAvatars_)
        {
            if (avatar != nullptr)
            {
                avatar->Release();
            }
        }
        quarantinedAvatars_.clear();
        movement_.Detach();
        entities_ = nullptr;
        npcs_ = nullptr;
        look_ = nullptr;
        presentationFactory_ = nullptr;
        diagnostics_ = {};
        nextSpawnAttemptAt_ = 0;
        initialized_ = false;
    }
}
