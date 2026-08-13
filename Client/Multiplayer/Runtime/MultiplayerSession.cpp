#include "MultiplayerSession.h"

#include "Automation/Runtime/RuntimeConfiguration.h"
#include "Game/Creature/Control/ScriptControl.h"
#include "Game/Creature/Locomotion/CreatureLocomotionService.h"
#include "Game/Creature/Locomotion/CreatureLocomotionState.h"
#include "Game/Creature/Look/CreatureLookService.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/HeroPawn/Appearance/Native/HeroAttachableAppearanceComponent.h"
#include "Game/HeroPawn/Appearance/Native/HeroClothingComponent.h"
#include "Game/HeroPawn/Appearance/Native/HeroMorphComponent.h"
#include "Game/NPC/NpcService.h"

#include <Windows.h>

#include <cmath>
#include <cstdio>

namespace
{
    constexpr float kMinimumVisiblePlayerSeparation = 1.25f;
    // The sidecar equips this ordinary creature definition with the Hero
    // presentation while preserving an NPC-compatible actor lifecycle.
    constexpr const char* kRemoteHeroDefinition =
        "CREATURE_HERO_RIVAL_GOOD_01";
    std::string Utf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }
        const int required = WideCharToMultiByte(
            CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1)
        {
            return {};
        }
        std::string result(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, value.c_str(), -1, result.data(), required, nullptr, nullptr);
        result.pop_back();
        return result;
    }

    std::uint64_t StablePlayerActorId(
        fable::multiplayer::PeerRole role,
        const std::string& playerId) noexcept
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (const unsigned char character : playerId)
        {
            hash ^= character;
            hash *= 1099511628211ull;
        }
        hash ^= static_cast<std::uint8_t>(role);
        hash *= 1099511628211ull;
        return hash == 0 ? 1 : hash;
    }

    bool ReportHeroSkeletalPresentation(
        const char* label,
        void* nativeThing,
        const fable::core::Diagnostics& diagnostics)
    {
        fable::game::hero_pawn::appearance::native::HeroMorphResolutionState
            state;
        const bool resolved =
            fable::game::hero_pawn::appearance::native::HeroMorphComponent::
                InspectResolution(nativeThing, state);
        char detail[640] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "%s resolved=%s thing=%p hero_morph=%p graphic=%p graphic_vtable=%p bridge=%p bridge_vtable=%p pawn=%p skeletal_mesh=%p anim_tree=%p mass_bone_scaling=%p reference_bones=%d scales=%d morph_targets=%d morph_nodes=%d weighted_morph_nodes=%d max_morph_weight=%.4f composite_commands=%d composite_hash=%08X first_composite=(%08X,%08X,%08X,%.4f)",
            label,
            resolved ? "true" : "false",
            state.thing,
            state.heroMorphComponent,
            state.graphic,
            state.graphicVtable,
            state.graphicBridge,
            state.graphicBridgeVtable,
            state.pawn,
            state.skeletalMeshComponent,
            state.animTree,
            state.massBoneScaling,
            state.referenceBoneCount,
            state.scaleCount,
            state.morphTargetCount,
            state.morphNodeCount,
            state.weightedMorphNodeCount,
            state.maximumMorphNodeWeight,
            state.compositeTextureCommandCount,
            state.compositeTextureCommandHash,
            state.firstCompositeTextureCommand[0],
            state.firstCompositeTextureCommand[1],
            state.firstCompositeTextureCommand[2],
            state.firstCompositeTextureWeight);
        diagnostics.Event("MultiplayerHeroSkeletalPresentation", detail);
        return resolved;
    }
}

namespace fable::multiplayer
{
    MultiplayerSession::~MultiplayerSession()
    {
        Shutdown();
    }

    bool MultiplayerSession::Initialize(
        const automation::runtime::RuntimeConfiguration& configuration,
        game::EntityService& entities,
        game::NpcService& npcs,
        game::creature::locomotion::CreatureLocomotionService& locomotion,
        game::creature::look::CreatureLookService& look,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        if (!configuration.MultiplayerEnabled())
        {
            return true;
        }

        entities_ = &entities;
        npcs_ = &npcs;
        locomotion_ = &locomotion;
        look_ = &look;
        diagnostics_ = diagnostics;
        role_ = configuration.MultiplayerRole() == L"host"
            ? PeerRole::Host
            : PeerRole::Guest;
        playerId_ = Utf8(configuration.MultiplayerPlayerId());
        appearanceDefinition_ = Utf8(configuration.MultiplayerAppearance());
        morphSelfTest_ = configuration.MorphSelfTest();
        localActorId_ = StablePlayerActorId(role_, playerId_);

        if (!remoteHeroPresentationFactory_.Install(
                entities.GameModule(), diagnostics_))
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-remote-hero-presentation-factory-hook");
            Shutdown();
            return false;
        }

        const bool started = role_ == PeerRole::Host
            ? transport_.StartHost(configuration.MultiplayerPort(), diagnostics_)
            : transport_.StartGuest(
                Utf8(configuration.MultiplayerAddress()),
                configuration.MultiplayerPort(),
                diagnostics_);
        if (!started)
        {
            diagnostics_.Event("ClientFailed", "multiplayer-transport-start");
            Shutdown();
            return false;
        }

        enabled_ = true;
        char detail[320] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "role=%s player=%s actor_id=%llu authority_epoch=1 appearance=%s",
            role_ == PeerRole::Host ? "host" : "guest",
            playerId_.c_str(),
            static_cast<unsigned long long>(localActorId_),
            appearanceDefinition_.c_str());
        diagnostics_.Event("MultiplayerSessionReady", detail);
        return true;
    }

    bool MultiplayerSession::OnWorldReady()
    {
        if (!enabled_ || worldReady_ || worldEntryPending_)
        {
            return true;
        }
        worldEntryPending_ = true;
        locomotion_->SetPlayerFrameObserver(
            &MultiplayerSession::ObservePlayerFrame,
            this);
        diagnostics_.Event(
            "MultiplayerOwnerPresentationPending",
            "selected-save Hero exists; waiting for its UE3 skeletal presentation and AnimTree");
        return true;
    }

    bool MultiplayerSession::BindLocalHero()
    {
        game::Entity* const currentHero = entities_->GetHero();
        if (currentHero == nullptr || !currentHero->IsValid())
        {
            if (currentHero != nullptr)
            {
                currentHero->Release();
            }
            return false;
        }
        void* const currentNative = entities_->ResolveNative(
            currentHero->NativeHandle());
        if (!game::creature::native::CreatureFrameFunctions::ValidatePlayerCreature(
                entities_->GameModule(),
                currentNative))
        {
            currentHero->Release();
            return false;
        }
        if (worldTransitionReported_ && currentNative == departingNativeHero_)
        {
            // GetHero can briefly continue returning the departing world's
            // object while UE3 drains its level teardown. Never reopen an
            // actor channel against that half-destroyed native graph.
            currentHero->Release();
            return false;
        }
        if (hero_ != nullptr)
        {
            hero_->Release();
        }
        hero_ = currentHero;
        nativeHero_ = currentNative;

        localMapName_ = hero_->GetCurrentMapName();
        const game::Vector3 position = hero_->GetPosition();
        game::hero_pawn::appearance::HeroMorphState heroMorph;
        if (!game::hero_pawn::appearance::native::HeroMorphComponent::Capture(
                nativeHero_, heroMorph))
        {
            return false;
        }
        game::hero_pawn::appearance::HeroBoneScaleState heroBoneScales;
        const bool scalesReady =
            game::hero_pawn::appearance::native::HeroMorphComponent::
                CaptureBoneScaleState(nativeHero_, heroBoneScales);
        game::hero_pawn::appearance::HeroClothingState heroClothing;
        const bool clothingReady =
            game::hero_pawn::appearance::native::HeroClothingComponent::Capture(
                nativeHero_, heroClothing);
        game::hero_pawn::appearance::HeroAppearanceModifierState
            heroAppearanceModifiers;
        const bool modifiersReady =
            game::hero_pawn::appearance::native::
                HeroAttachableAppearanceComponent::Capture(
                    nativeHero_, heroAppearanceModifiers);
        if (scalesReady && morphSelfTest_)
        {
            if (!game::hero_pawn::appearance::native::HeroMorphComponent::
                    ApplyBoneScaleState(nativeHero_, heroBoneScales))
            {
                diagnostics_.Event("ClientFailed", "multiplayer-owner-bone-scale-self-apply");
                ReleaseLocalHero();
                return false;
            }
            diagnostics_.Event(
                "MultiplayerOwnerBoneScalesSelfApplied",
                "captured MassBoneScaling vectors were applied back to their source Hero");
        }
        playerChannel_.OpenOwner(
            localActorId_,
            1,
            role_,
            playerId_,
            appearanceDefinition_,
            heroMorph,
            heroClothing,
            heroBoneScales,
            heroAppearanceModifiers,
            localMapName_,
            position,
            hero_->GetFacing(),
            GetTickCount64());
        PlayerState baseline;
        if (!playerChannel_.TakeDirtyUpdate(baseline) ||
            !transport_.Submit(baseline))
        {
            diagnostics_.Event("ClientFailed", "multiplayer-owner-baseline");
            ReleaseLocalHero();
            return false;
        }

        worldReady_ = true;
        worldEntryPending_ = false;
        const bool completedWorldTransition = worldTransitionReported_;
        worldTransitionReported_ = false;
        departingNativeHero_ = nullptr;
        ReleaseDeferredWorldPresentations();
        ownerAppearanceReady_ = scalesReady && clothingReady && modifiersReady;

        char detail[448] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "player=%s actor_id=%llu local_presentation=selected-save-hero remote_definition=%s map=%s position=(%.3f,%.3f,%.3f) morph=(%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f) child=%s",
            playerId_.c_str(),
            static_cast<unsigned long long>(localActorId_),
            appearanceDefinition_.c_str(),
            localMapName_.c_str(),
            position.x,
            position.y,
            position.z,
            heroMorph.strength,
            heroMorph.berserk,
            heroMorph.will,
            heroMorph.skill,
            heroMorph.age,
            heroMorph.alignment,
            heroMorph.fatness,
            heroMorph.child ? "true" : "false");
        diagnostics_.Event("MultiplayerLocalHeroReady", detail);
        if (completedWorldTransition)
        {
            diagnostics_.Event(
                "MultiplayerWorldTransitionCompleted",
                "destination selected-save Hero rebound; map-scoped presentations may be recreated");
        }
        if (clothingReady)
        {
            char clothingDetail[160] = {};
            std::snprintf(
                clothingDetail,
                sizeof(clothingDetail),
                "selected=(%d,%d,%d,%d,%d,%d) source=selected-save-hero",
                heroClothing.definitionIndices[0],
                heroClothing.definitionIndices[1],
                heroClothing.definitionIndices[2],
                heroClothing.definitionIndices[3],
                heroClothing.definitionIndices[4],
                heroClothing.definitionIndices[5]);
            diagnostics_.Event("MultiplayerLocalHeroClothing", clothingDetail);
        }
        const std::size_t reportedScaleEntries = std::min<std::size_t>(
            heroBoneScales.count, 4);
        for (std::size_t index = 0; index < reportedScaleEntries; ++index)
        {
            const auto& entry = heroBoneScales.entries[index];
            char scaleDetail[256] = {};
            std::snprintf(
                scaleDetail,
                sizeof(scaleDetail),
                "count=%u index=%u reference_bone=%u scale=(%.6f,%.6f,%.6f)",
                heroBoneScales.count,
                static_cast<unsigned int>(index),
                entry.boneIndex,
                entry.x,
                entry.y,
                entry.z);
            diagnostics_.Event("MultiplayerLocalHeroBoneScale", scaleDetail);
        }
        diagnostics_.Event(
            "MultiplayerOwnerChannelOpened",
            scalesReady
                ? "selected-save Hero owns a bounded lifecycle-scoped replication channel with appearance"
                : "selected-save Hero owns identity, map, and movement; skeletal appearance is pending");
        return true;
    }

    void MultiplayerSession::ObservePlayerFrame(
        void* context,
        void* playerCreature)
    {
        auto* const session = static_cast<MultiplayerSession*>(context);
        if (session != nullptr)
        {
            session->OnPlayerFrame(playerCreature);
        }
    }

    bool MultiplayerSession::ReadReplicatedMovement(
        void* context,
        void* creature,
        game::creature::look::CreatureFacingInputRouterHook::ReplicatedMovementInput& input)
    {
        auto* const session = static_cast<MultiplayerSession*>(context);
        return session != nullptr &&
            session->ProvideRemoteMovement(creature, input);
    }

    void MultiplayerSession::OnPlayerFrame(void* playerCreature)
    {
        if (!enabled_)
        {
            return;
        }
        if (!worldReady_)
        {
            if (worldEntryPending_ &&
                game::creature::native::CreatureFrameFunctions::
                    ValidatePlayerCreature(
                        entities_->GameModule(), playerCreature) &&
                BindLocalHero())
            {
                diagnostics_.Event(
                    "MultiplayerOwnerPresentationBound",
                    ownerAppearanceReady_
                        ? "selected-save Hero and optional morph presentation are ready"
                        : "selected-save Hero actor channel is ready; optional morph translation is pending");
            }
            return;
        }
        if (!ownerGraphicRuntimeReported_)
        {
            ownerGraphicRuntimeReported_ = ReportHeroSkeletalPresentation(
                "owner", nativeHero_, diagnostics_);
        }
        const std::uint64_t now = GetTickCount64();
        CaptureAndReplicateOwnerState(now);

        if (transport_.HasFailed() && !transportFailureReported_)
        {
            transportFailureReported_ = true;
            diagnostics_.Event("ClientFailed", "multiplayer-network-worker");
        }
    }

    void MultiplayerSession::CaptureAndReplicateOwnerState(std::uint64_t now)
    {
        if (hero_ == nullptr || !hero_->IsValid())
        {
            return;
        }
        std::string mapName = hero_->GetCurrentMapName();
        if (mapName.empty())
        {
            mapName = localMapName_;
        }
        PlayerState update;
        {
            std::lock_guard<std::mutex> lock(ownerStateMutex_);
            playerChannel_.CaptureOwnerMovement(
                mapName,
                hero_->GetPosition(),
                hero_->GetFacing(),
                now);
            if (!playerChannel_.TakeDirtyUpdate(update))
            {
                return;
            }
        }
        if (!transport_.Submit(update))
        {
            if (!transportFailureReported_)
            {
                transportFailureReported_ = true;
                diagnostics_.Event("ClientFailed", "multiplayer-owner-property-submit");
            }
            return;
        }
        if (!exchangeReported_)
        {
            exchangeReported_ = true;
            diagnostics_.Event(
                "MultiplayerReplicationStarted",
                "owner property mutation submitted to the network actor channel");
        }
    }

    void MultiplayerSession::CaptureAndReplicateOwnerAppearance(
        std::uint64_t now)
    {
        if (ownerAppearanceReady_ || nativeHero_ == nullptr ||
            now < nextOwnerAppearanceCaptureAt_)
        {
            return;
        }
        nextOwnerAppearanceCaptureAt_ = now + 500;
        game::hero_pawn::appearance::HeroMorphState heroMorph;
        game::hero_pawn::appearance::HeroClothingState heroClothing;
        game::hero_pawn::appearance::HeroBoneScaleState heroBoneScales;
        game::hero_pawn::appearance::HeroAppearanceModifierState
            heroAppearanceModifiers;
        if (!game::hero_pawn::appearance::native::HeroMorphComponent::Capture(
                nativeHero_, heroMorph) ||
            !game::hero_pawn::appearance::native::HeroMorphComponent::
                CaptureBoneScaleState(nativeHero_, heroBoneScales) ||
            !game::hero_pawn::appearance::native::HeroClothingComponent::Capture(
                nativeHero_, heroClothing) ||
            !game::hero_pawn::appearance::native::
                HeroAttachableAppearanceComponent::Capture(
                    nativeHero_, heroAppearanceModifiers))
        {
            if (now >= nextOwnerBindDiagnosticAt_)
            {
                nextOwnerBindDiagnosticAt_ = now + 5'000;
                game::hero_pawn::appearance::native::HeroMorphResolutionState
                    resolution;
                const bool resolved =
                    game::hero_pawn::appearance::native::HeroMorphComponent::
                        InspectResolution(nativeHero_, resolution);
                char detail[512] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "resolved=%s thing=%p hero_morph=%p graphic=%p graphic_vtable=%p bridge=%p bridge_vtable=%p pawn=%p skeletal_mesh=%p anim_tree=%p mass_bone_scaling=%p reference_bones=%d scales=%d",
                    resolved ? "true" : "false",
                    resolution.thing,
                    resolution.heroMorphComponent,
                    resolution.graphic,
                    resolution.graphicVtable,
                    resolution.graphicBridge,
                    resolution.graphicBridgeVtable,
                    resolution.pawn,
                    resolution.skeletalMeshComponent,
                    resolution.animTree,
                    resolution.massBoneScaling,
                    resolution.referenceBoneCount,
                    resolution.scaleCount);
                diagnostics_.Event("MultiplayerOwnerPresentationWaiting", detail);
            }
            return;
        }
        if (!playerChannel_.CaptureOwnerAppearance(
                appearanceDefinition_,
                heroMorph,
                heroClothing,
                heroBoneScales,
                heroAppearanceModifiers))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-owner-appearance-capture");
            return;
        }
        PlayerState update;
        if (!playerChannel_.TakeDirtyUpdate(update) ||
            !transport_.Submit(update))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-owner-appearance-submit");
            return;
        }
        ownerAppearanceReady_ = true;
        char detail[160] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "clothing=(%d,%d,%d,%d,%d,%d) bone_scale_count=%u modifier_count=%u source=selected-save-hero",
            heroClothing.definitionIndices[0],
            heroClothing.definitionIndices[1],
            heroClothing.definitionIndices[2],
            heroClothing.definitionIndices[3],
            heroClothing.definitionIndices[4],
            heroClothing.definitionIndices[5],
            heroBoneScales.count,
            heroAppearanceModifiers.count);
        diagnostics_.Event("MultiplayerOwnerAppearanceReplicated", detail);
    }

    bool MultiplayerSession::ProcessPresentationLifecycle()
    {
        if (!enabled_)
        {
            return false;
        }
        if (!worldReady_)
        {
            if (worldEntryPending_ && BindLocalHero())
            {
                diagnostics_.Event(
                    "MultiplayerOwnerPresentationBound",
                    ownerAppearanceReady_
                        ? "selected-save Hero and optional morph presentation are ready"
                        : "selected-save Hero actor channel is ready; optional morph translation is pending");
            }
            return false;
        }

        // A map transition invalidates the native creature/component graph
        // before every retained CScriptThing necessarily reports null.  The
        // remote proxy must leave our hooks while its old world is still
        // current; otherwise a queued creature frame can query a component
        // whose vtable has already been poisoned by level teardown.
        if (!LocalWorldIsCurrent())
        {
            BeginWorldTransition();
            return true;
        }

        PlayerState inbound;
        const std::uint64_t now = GetTickCount64();
        CaptureAndReplicateOwnerAppearance(now);
        if (transport_.TryConsume(inbound))
        {
            std::lock_guard<std::mutex> lock(remoteStateMutex_);
            if (playerChannel_.ApplyRemoteUpdate(inbound))
            {
                remoteSampleReceivedAt_.store(now, std::memory_order_release);
                if (!remoteStateAppliedReported_)
                {
                    remoteStateAppliedReported_ = true;
                    char detail[384] = {};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "player=%s role=%s actor_id=%llu properties=0x%08X map=%s position=(%.3f,%.3f,%.3f)",
                        inbound.playerId.c_str(),
                        inbound.role == PeerRole::Host ? "host" : "guest",
                        static_cast<unsigned long long>(inbound.actorId),
                        inbound.changedProperties,
                        inbound.mapName.c_str(),
                        inbound.position.x,
                        inbound.position.y,
                        inbound.position.z);
                    diagnostics_.Event("MultiplayerRemoteStateApplied", detail);
                }
            }
        }

        PlayerState state;
        {
            std::lock_guard<std::mutex> lock(remoteStateMutex_);
            const PlayerState* const current = playerChannel_.RemoteState();
            if (current == nullptr)
            {
                return false;
            }
            state = *current;
        }
        ReconcileRemotePresentation(state);
        return false;
    }

    void MultiplayerSession::ReconcileRemotePresentation(const PlayerState& state)
    {
        if (state.actorId == 0 || state.actorId == localActorId_ ||
            state.playerId.empty() || state.appearanceDefinition.empty())
        {
            return;
        }
        if (state.mapName.empty() || state.mapName != localMapName_)
        {
            SuspendRemoteAvatar(state);
            return;
        }
        if (remoteAvatarSuspended_ && !ResumeRemoteAvatar(state))
        {
            RetireRemoteAvatar();
        }
        const std::uint64_t now = GetTickCount64();
        if (remoteAvatar_ == nullptr && now < nextRemoteSpawnAttemptAt_)
        {
            return;
        }
        if (remoteAvatar_ == nullptr || !remoteAvatar_->IsValid() ||
            remotePlayerId_ != state.playerId ||
            remoteAppearanceDefinition_ != state.appearanceDefinition)
        {
            if (!SpawnRemoteAvatar(state))
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
                InspectResolution(nativeRemoteAvatar_, resolution))
        {
            // The native clothing callback submits drawable attachments to the
            // Hero pawn's skeletal presentation. Applying it before the proxy
            // graphic bridge exists consumes the component's dirty state while
            // there is nowhere to attach the submitted meshes. Preserve the
            // actor and retry once its normal presentation lifecycle is ready.
            return;
        }
        if (!remoteGraphicRuntimeReported_)
        {
            remoteGraphicRuntimeReported_ = ReportHeroSkeletalPresentation(
                "remote", nativeRemoteAvatar_, diagnostics_);
        }
        if (appearanceReady &&
            !remoteAppliedClothing_.Equals(state.heroClothing))
        {
            std::uint32_t inserted = 0;
            if (!game::hero_pawn::appearance::native::HeroClothingComponent::Apply(
                    nativeRemoteAvatar_, state.heroClothing, &inserted))
            {
                // Clothing state and its native definition manager bind after
                // actor construction. Preserve the actor and retry next frame.
                return;
            }
            remoteAppliedClothing_ = state.heroClothing;
            char clothingDetail[224] = {};
            std::snprintf(
                clothingDetail,
                sizeof(clothingDetail),
                "selected=(%d,%d,%d,%d,%d,%d) inserted=%u operation=native-wear-rebuild",
                state.heroClothing.definitionIndices[0],
                state.heroClothing.definitionIndices[1],
                state.heroClothing.definitionIndices[2],
                state.heroClothing.definitionIndices[3],
                state.heroClothing.definitionIndices[4],
                state.heroClothing.definitionIndices[5],
                inserted);
            diagnostics_.Event("MultiplayerRemoteClothingApplied", clothingDetail);
        }
        if (appearanceReady &&
            !remoteAppliedAppearanceModifiers_.Equals(
                state.heroAppearanceModifiers))
        {
            std::uint32_t removed = 0;
            std::uint32_t added = 0;
            if (!game::hero_pawn::appearance::native::
                    HeroAttachableAppearanceComponent::Apply(
                        nativeRemoteAvatar_,
                        state.heroAppearanceModifiers,
                        &removed,
                        &added))
            {
                // The component and definition manager finish binding during
                // the remote Hero's normal presentation lifecycle.
                return;
            }
            remoteAppliedAppearanceModifiers_ =
                state.heroAppearanceModifiers;
            char modifierDetail[192] = {};
            std::snprintf(
                modifierDetail,
                sizeof(modifierDetail),
                "source=%u removed=%u added=%u operation=component-refresh",
                state.heroAppearanceModifiers.count,
                removed,
                added);
            diagnostics_.Event(
                "MultiplayerRemoteAppearanceModifiersApplied",
                modifierDetail);
        }
        if (appearanceReady &&
            !remoteAppliedMorph_.Equals(state.heroMorph))
        {
            if (!game::hero_pawn::appearance::native::HeroMorphComponent::
                    ApplyValues(nativeRemoteAvatar_, state.heroMorph))
            {
                diagnostics_.Event(
                    "ClientFailed", "multiplayer-remote-morph-refresh");
                RetireRemoteAvatar();
                return;
            }
            remoteAppliedMorph_ = state.heroMorph;
        }
        if (appearanceReady &&
            !remoteAppliedBoneScales_.Equals(state.heroBoneScales))
        {
            std::uint32_t matchedScales = 0;
            if (!game::hero_pawn::appearance::native::HeroMorphComponent::
                    ApplyBoneScaleState(
                        nativeRemoteAvatar_, state.heroBoneScales, &matchedScales))
            {
                diagnostics_.Event(
                    "ClientFailed", "multiplayer-remote-bone-scale-refresh");
                RetireRemoteAvatar();
                return;
            }
            remoteAppliedBoneScales_ = state.heroBoneScales;
            char detail[160] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "matched=%u source=%u operation=refresh",
                matchedScales,
                state.heroBoneScales.count);
            diagnostics_.Event("MultiplayerRemoteBoneScalesApplied", detail);
        }
        if (!remoteSeparationReported_ && hero_ != nullptr && hero_->IsValid())
        {
            const game::Vector3 localPosition = hero_->GetPosition();
            const game::Vector3 remotePosition = remoteAvatar_->GetPosition();
            const float separation = localPosition.HorizontalDistanceTo(
                remotePosition);
            if (separation >= kMinimumVisiblePlayerSeparation)
            {
                remoteSeparationReported_ = true;
                char detail[384] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "local=(%.3f,%.3f,%.3f) remote=(%.3f,%.3f,%.3f) separation=%.3f",
                    localPosition.x,
                    localPosition.y,
                    localPosition.z,
                    remotePosition.x,
                    remotePosition.y,
                    remotePosition.z,
                    separation);
                diagnostics_.Event("MultiplayerRemoteAvatarSeparated", detail);
            }
        }
    }

    bool MultiplayerSession::SpawnRemoteAvatar(const PlayerState& state)
    {
        RetireRemoteAvatar();
        nextRemoteSpawnAttemptAt_ = GetTickCount64() + 5'000;
        remoteHeroPresentationFactory_.Arm(state.position);
        remoteAvatar_ = npcs_->Spawn(
            state.appearanceDefinition,
            state.position,
            "SCRIPT_NAME_FABLE_TOGETHER_REMOTE_PLAYER");
        if (remoteAvatar_ == nullptr || !remoteAvatar_->IsValid() ||
            remoteAvatar_->GetDefinitionName() != state.appearanceDefinition)
        {
            diagnostics_.Event("ClientFailed", "multiplayer-remote-avatar-spawn");
            RetireRemoteAvatar();
            return false;
        }
        nativeRemoteAvatar_ = entities_->ResolveNative(remoteAvatar_->NativeHandle());
        game::hero_pawn::appearance::native::HeroMorphResolutionState
            pendingPresentation;
        const bool presentationAlreadyReady =
            game::hero_pawn::appearance::native::HeroMorphComponent::
                InspectResolution(nativeRemoteAvatar_, pendingPresentation);
        (void)presentationAlreadyReady;
        if (pendingPresentation.graphic != nullptr)
        {
            remoteHeroPresentationFactory_.TargetGraphic(
                pendingPresentation.graphic);
        }
        if (!game::creature::native::CreatureFrameFunctions::ValidateCreature(
                entities_->GameModule(),
                nativeRemoteAvatar_))
        {
            diagnostics_.Event("ClientFailed", "multiplayer-remote-native-type");
            RetireRemoteAvatar();
            return false;
        }
        if (game::entity::native::ThingComponentAccess::Has(
                nativeRemoteAvatar_,
                game::entity::native::ThingComponentType::HeroMorph) &&
            !game::hero_pawn::appearance::native::HeroMorphComponent::
                SetUpdateRequested(nativeRemoteAvatar_, false))
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-remote-hero-morph-suppression");
            RetireRemoteAvatar();
            return false;
        }
        const bool appearanceReady = state.heroMorph.IsSane() &&
            state.heroClothing.IsSane() &&
            state.heroBoneScales.IsSane() &&
            state.heroAppearanceModifiers.IsSane();
        if (appearanceReady &&
            !game::hero_pawn::appearance::native::HeroMorphComponent::
                ApplyValues(nativeRemoteAvatar_, state.heroMorph))
        {
            diagnostics_.Event("ClientFailed", "multiplayer-remote-morph-values");
            RetireRemoteAvatar();
            return false;
        }
        if (appearanceReady)
        {
            remoteAppliedMorph_ = state.heroMorph;
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

        remotePlayerId_ = state.playerId;
        remoteAppearanceDefinition_ = state.appearanceDefinition;
        const bool presentationReady =
            remoteAvatar_->SetKillOnLevelUnload(true) &&
            remoteAvatar_->SetAttackable(false) &&
            remoteAvatar_->SetDamageable(false) &&
            remoteAvatar_->SetFriendsWithEverything(true) &&
            remoteAvatar_->SetCollidable(true) &&
            remoteAvatar_->SetDrawable(true);
        if (!presentationReady)
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-remote-presentation-flags");
            RetireRemoteAvatar();
            return false;
        }
        // The sidecar body deliberately retains an ordinary creature brain so
        // its construction and graphics lifetime remain valid. Seize that
        // brain immediately; replicated movement owns it from this point.
        const bool needsScriptControl = true;
        if (needsScriptControl)
        {
            remoteControl_ = npcs_->TakeControl(
                remoteAvatar_,
                game::AiPriority::Highest);
            if (remoteControl_ == nullptr || !remoteControl_->ClearCommands())
            {
                diagnostics_.Event(
                    "ClientFailed",
                    "multiplayer-remote-avatar-control");
                RetireRemoteAvatar();
                return false;
            }
        }
        if (!look_->RouteReplicatedMovement(
                remoteAvatar_,
                &MultiplayerSession::ReadReplicatedMovement,
                this))
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-remote-movement-routing");
            RetireRemoteAvatar();
            return false;
        }

        remoteLocomotionStartPosition_ = state.position;
        remoteLocomotionStartAnimationHash_ = 0;
        remoteMovementCommanded_ = false;
        remoteWalkingReported_ = false;
        lastRemoteObservationAt_ = 0;
        if (auto* locomotionState = locomotion_->Inspect(remoteAvatar_))
        {
            remoteLocomotionStartAnimationHash_ =
                locomotionState->AnimationStateHash();
            locomotionState->Release();
        }

        char detail[384] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "player=%s actor_id=%llu authority_epoch=%u definition=%s exact=true map=%s control=%s",
            remotePlayerId_.c_str(),
            static_cast<unsigned long long>(state.actorId),
            state.authorityEpoch,
            remoteAppearanceDefinition_.c_str(),
            state.mapName.c_str(),
            needsScriptControl ? "scripted" : "native-frame");
        diagnostics_.Event("MultiplayerRemoteDefinitionCreated", detail);
        diagnostics_.Event("MultiplayerRemoteAvatarReady", detail);
        diagnostics_.Event(
            "MultiplayerPresentationChannelOpened",
            "remote native creature presentation is scoped to the shared map lifecycle");
        nextRemoteSpawnAttemptAt_ = 0;
        return true;
    }

    void MultiplayerSession::SuspendRemoteAvatar(
        const PlayerState& state) noexcept
    {
        if (remoteAvatar_ == nullptr || remoteAvatarSuspended_)
        {
            return;
        }
        // A peer leaving our map does not unload our UE3 world. Destroying its
        // creature here races already-queued creature worker updates. Keep the
        // lifecycle-scoped proxy dormant and invisible so it can be resumed if
        // that peer returns before our own level is torn down.
        if (remoteControl_ != nullptr)
        {
            remoteControl_->ClearAllActions(true);
        }
        if (remoteAvatar_->IsValid())
        {
            remoteAvatar_->SetCollidable(false);
            remoteAvatar_->SetDrawable(false);
        }
        remoteAvatarSuspended_ = true;
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "player=%s remote_map=%s local_map=%s action=dormant",
            state.playerId.c_str(),
            state.mapName.c_str(),
            localMapName_.c_str());
        diagnostics_.Event("MultiplayerRemoteAvatarSuspended", detail);
    }

    bool MultiplayerSession::ResumeRemoteAvatar(const PlayerState& state)
    {
        if (remoteAvatar_ == nullptr || !remoteAvatarSuspended_ ||
            !remoteAvatar_->IsValid())
        {
            return false;
        }
        if (!remoteAvatar_->SetCollidable(true) ||
            !remoteAvatar_->SetDrawable(true))
        {
            return false;
        }
        remoteAvatarSuspended_ = false;
        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "player=%s map=%s action=resumed",
            state.playerId.c_str(),
            state.mapName.c_str());
        diagnostics_.Event("MultiplayerRemoteAvatarResumed", detail);
        return true;
    }

    bool MultiplayerSession::LocalWorldIsCurrent() const
    {
        if (hero_ == nullptr || !hero_->IsValid())
        {
            return false;
        }
        const std::string currentMap = hero_->GetCurrentMapName();
        return !currentMap.empty() && currentMap == localMapName_;
    }

    void MultiplayerSession::BeginWorldTransition() noexcept
    {
        if (!worldReady_ && !worldEntryPending_)
        {
            return;
        }
        if (!worldTransitionReported_)
        {
            worldTransitionReported_ = true;
            departingNativeHero_ = nativeHero_;
            diagnostics_.Event(
                "MultiplayerWorldTransitionStarted",
                "local selected-save Hero left its bound map; remote routing detached before UE3 level teardown");
        }
        RetireRemoteAvatar(true);
        ReleaseLocalHero();
        // WorldReady is edge-triggered by the native Hero observation.  Allow
        // the newly constructed Hero in the destination world to raise it.
        worldEntryPending_ = true;
    }

    bool MultiplayerSession::ProvideRemoteMovement(
        void* creature,
        game::creature::look::CreatureFacingInputRouterHook::ReplicatedMovementInput& input)
    {
        if (!enabled_ || !worldReady_ || creature != nativeRemoteAvatar_ ||
            remoteAvatar_ == nullptr || !remoteAvatar_->IsValid())
        {
            return false;
        }
        PlayerState state;
        {
            std::lock_guard<std::mutex> lock(remoteStateMutex_);
            const PlayerState* const current = playerChannel_.RemoteState();
            if (current == nullptr || current->mapName != localMapName_)
            {
                return false;
            }
            state = *current;
        }
        if (state.mapName != localMapName_)
        {
            return false;
        }

        if (!remoteMovementCommanded_ &&
            (state.moving ||
                remoteAvatar_->GetPosition().HorizontalDistanceTo(state.position) >= 0.05f))
        {
            remoteMovementCommanded_ = true;
            remoteLocomotionStartPosition_ = remoteAvatar_->GetPosition();
            if (auto* locomotionState = locomotion_->Inspect(remoteAvatar_))
            {
                remoteLocomotionStartAnimationHash_ =
                    locomotionState->AnimationStateHash();
                locomotionState->Release();
            }
            diagnostics_.Event(
                "MultiplayerRemoteMovementConsumed",
                "remote creature frame consumed an owner-authored movement property");
        }

        const std::uint64_t now = GetTickCount64();
        input.position = state.position;
        input.velocity = state.velocity;
        input.facing = state.facing;
        input.moving = state.moving;
        const std::uint64_t sampleReceivedAt = remoteSampleReceivedAt_.load(
            std::memory_order_acquire);
        input.sampleAgeSeconds = sampleReceivedAt == 0 ||
            now <= sampleReceivedAt
                ? 0.0f
                : static_cast<float>(now - sampleReceivedAt) / 1000.0f;
        ObserveRemoteLocomotion();
        return true;
    }

    void MultiplayerSession::ObserveRemoteLocomotion()
    {
        const std::uint64_t now = GetTickCount64();
        if (!remoteMovementCommanded_ || remoteWalkingReported_ ||
            remoteAvatar_ == nullptr ||
            (lastRemoteObservationAt_ != 0 &&
                now - lastRemoteObservationAt_ < 100))
        {
            return;
        }
        lastRemoteObservationAt_ = now;
        game::creature::locomotion::CreatureLocomotionState* state =
            locomotion_->Inspect(remoteAvatar_);
        if (state == nullptr)
        {
            return;
        }
        const bool displaced = state->PhysicsPosition().HorizontalDistanceTo(
            remoteLocomotionStartPosition_) >= 0.20f;
        const bool animated = remoteLocomotionStartAnimationHash_ != 0 &&
            state->AnimationStateHash() != 0 &&
            state->AnimationStateHash() != remoteLocomotionStartAnimationHash_;
        const bool fullStack = state->HasPhysicsNavigator() &&
            state->HasCreatureNavigation() &&
            state->HasAnimationComplex();
        state->Release();
        if (!displaced || !animated || !fullStack)
        {
            return;
        }
        remoteWalkingReported_ = true;
        diagnostics_.Event(
            "MultiplayerRemoteAvatarWalking",
            "owner movement property produced remote native physics displacement and animation-complex activity");
    }

    void MultiplayerSession::RetireRemoteAvatar(bool worldUnloading) noexcept
    {
        remoteHeroPresentationFactory_.Cancel();
        if (look_ != nullptr && remoteAvatar_ != nullptr)
        {
            // Never call ResetForceLookAt while the level is dismantling the
            // creature's component range. Clearing our binding and retained
            // reference is sufficient; UE3 owns the unload destruction.
            look_->StopRouting(remoteAvatar_, !worldUnloading);
        }
        if (worldUnloading &&
            (remoteAvatar_ != nullptr || remoteControl_ != nullptr))
        {
            // UE3 owns destruction of SetKillOnLevelUnload creatures. Keep our
            // counted wrappers alive through the old world's worker drain and
            // release them only after a different native Hero is bound.
            deferredWorldPresentation_ = {remoteAvatar_, remoteControl_};
            remoteAvatar_ = nullptr;
            remoteControl_ = nullptr;
        }
        if (remoteControl_ != nullptr)
        {
            if (!worldUnloading)
            {
                remoteControl_->ClearAllActions(true);
                remoteControl_->ReleaseControl();
            }
            remoteControl_->Release();
            remoteControl_ = nullptr;
        }
        if (remoteAvatar_ != nullptr)
        {
            if (!worldUnloading && remoteAvatar_->IsValid())
            {
                remoteAvatar_->SetCollidable(false);
                remoteAvatar_->SetDrawable(false);
            }
            remoteAvatar_->Release();
            remoteAvatar_ = nullptr;
        }
        nativeRemoteAvatar_ = nullptr;
        remotePlayerId_.clear();
        remoteAppearanceDefinition_.clear();
        remoteAppliedMorph_ = {};
        remoteAppliedClothing_ = {};
        remoteAppliedBoneScales_ = {};
        remoteAppliedAppearanceModifiers_ = {};
        remoteGraphicRuntimeReported_ = false;
        remoteAvatarSuspended_ = false;
        remoteMovementCommanded_ = false;
        remoteWalkingReported_ = false;
        remoteSeparationReported_ = false;
        lastRemoteObservationAt_ = 0;
    }

    void MultiplayerSession::ReleaseDeferredWorldPresentations() noexcept
    {
        if (deferredWorldPresentation_.control != nullptr)
        {
            deferredWorldPresentation_.control->Release();
            deferredWorldPresentation_.control = nullptr;
        }
        if (deferredWorldPresentation_.avatar != nullptr)
        {
            deferredWorldPresentation_.avatar->Release();
            deferredWorldPresentation_.avatar = nullptr;
        }
    }

    void MultiplayerSession::ReleaseLocalHero() noexcept
    {
        if (locomotion_ != nullptr)
        {
            locomotion_->SetPlayerFrameObserver(nullptr, nullptr);
        }
        nativeHero_ = nullptr;
        if (hero_ != nullptr)
        {
            hero_->Release();
            hero_ = nullptr;
        }
        worldReady_ = false;
        worldEntryPending_ = false;
        ownerAppearanceReady_ = false;
        nextOwnerBindDiagnosticAt_ = 0;
        nextOwnerAppearanceCaptureAt_ = 0;
        ownerGraphicRuntimeReported_ = false;
        localMapName_.clear();
    }

    void MultiplayerSession::Shutdown() noexcept
    {
        if (locomotion_ != nullptr)
        {
            locomotion_->SetPlayerFrameObserver(nullptr, nullptr);
        }
        RetireRemoteAvatar();
        ReleaseDeferredWorldPresentations();
        ReleaseLocalHero();
        playerChannel_.Close();
        transport_.Shutdown();
        entities_ = nullptr;
        npcs_ = nullptr;
        locomotion_ = nullptr;
        look_ = nullptr;
        diagnostics_ = {};
        enabled_ = false;
        localActorId_ = 0;
        nextRemoteSpawnAttemptAt_ = 0;
        remoteSampleReceivedAt_.store(0, std::memory_order_release);
        remoteSeparationReported_ = false;
        remoteStateAppliedReported_ = false;
        exchangeReported_ = false;
        transportFailureReported_ = false;
        morphSelfTest_ = false;
        ownerGraphicRuntimeReported_ = false;
        remoteGraphicRuntimeReported_ = false;
        worldTransitionReported_ = false;
        departingNativeHero_ = nullptr;
    }

    bool MultiplayerSession::IsEnabled() const noexcept
    {
        return enabled_;
    }

    bool MultiplayerSession::IsWorldReady() const noexcept
    {
        return worldReady_;
    }
}
