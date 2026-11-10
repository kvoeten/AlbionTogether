#include "RemoteHeroActor.h"

#include "Game/Entity/Entity.h"
#include "Multiplayer/Entities/LiveEntityRegistry.h"
#include "Multiplayer/Protocol/EquipmentTransitionTiming.h"
#include "RemoteHeroNativeLifecycle.h"

#include <Windows.h>

#include <cstdio>

namespace
{
    constexpr float kMinimumVisiblePlayerSeparation = 1.25f;
}

namespace fable::multiplayer::presentation::remote_hero
{
    using ::fable::multiplayer::PlayerState;
    namespace movement = ::fable::multiplayer::movement;

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
        if (appearanceResult ==
            appearance::RemoteHeroAppearanceResult::Failed)
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
            appearanceResult ==
                appearance::RemoteHeroAppearanceResult::Ready;
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
}
