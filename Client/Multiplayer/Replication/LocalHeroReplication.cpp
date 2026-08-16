#include "LocalHeroReplication.h"

#include "Game/Creature/Locomotion/CreatureLocomotionService.h"
#include "Game/Creature/Look/Native/CreatureLookFunctions.h"
#include "Game/Creature/Native/CreatureFrameFunctions.h"
#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/HeroPawn/Appearance/Native/HeroAttachableAppearanceComponent.h"
#include "Game/HeroPawn/Appearance/Native/HeroClothingComponent.h"
#include "Game/HeroPawn/Appearance/Native/HeroMorphComponent.h"
#include "Multiplayer/Presentation/HeroPresentationDiagnostics.h"
#include "Multiplayer/Replication/LocalPlayerChannel.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace fable::multiplayer::replication
{
    namespace
    {
        std::uint64_t ReadNativeThingUid(void* thing) noexcept
        {
            if (thing == nullptr)
            {
                return 0;
            }
            __try
            {
                return *reinterpret_cast<const std::uint64_t*>(
                    static_cast<const std::uint8_t*>(thing) + 0x14);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return 0;
            }
        }

        std::uint16_t ReadNativeThingMapId(void* thing) noexcept
        {
            if (thing == nullptr)
            {
                return 0;
            }
            __try
            {
                return *reinterpret_cast<const std::uint16_t*>(
                    static_cast<const std::uint8_t*>(thing) + 0x9A);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return 0;
            }
        }
    }

    void LocalHeroReplication::Initialize(
        game::EntityService& entities,
        game::creature::locomotion::CreatureLocomotionService& locomotion,
        LocalPlayerChannel& channel,
        UdpPeer& transport,
        const core::Diagnostics& diagnostics,
        PeerRole role,
        std::uint64_t actorId,
        std::string playerId,
        std::string appearanceDefinition,
        bool morphSelfTest)
    {
        Shutdown();
        entities_ = &entities;
        locomotion_ = &locomotion;
        channel_ = &channel;
        transport_ = &transport;
        diagnostics_ = diagnostics;
        role_ = role;
        actorId_ = actorId;
        playerId_ = std::move(playerId);
        appearanceDefinition_ = std::move(appearanceDefinition);
        morphSelfTest_ = morphSelfTest;
        initialized_ = true;
    }

    bool LocalHeroReplication::OnWorldReady()
    {
        if (!initialized_ || worldReady_ || entryPending_)
        {
            return true;
        }
        entryPending_ = true;
        locomotion_->SetPlayerFrameObserver(
            &LocalHeroReplication::ObservePlayerFrame,
            this);
        diagnostics_.Event(
            "MultiplayerOwnerPresentationPending",
            "selected-save Hero exists; waiting for its UE3 skeletal presentation and AnimTree");
        return true;
    }

    bool LocalHeroReplication::TryBind()
    {
        if (!initialized_ || !entryPending_)
        {
            return false;
        }
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
        const std::string currentMap = currentHero->GetCurrentMapName();
        if (!game::creature::native::CreatureFrameFunctions::ValidatePlayerCreature(
                entities_->GameModule(), currentNative) ||
            currentMap.empty() ||
            (transitionActive_ && currentNative == departingNativeHero_ &&
                currentMap == departingMapName_))
        {
            currentHero->Release();
            return false;
        }
        if (hero_ != nullptr)
        {
            hero_->Release();
        }
        hero_ = currentHero;
        nativeHero_ = currentNative;
        mapName_ = currentMap;
        mapId_ = ReadNativeThingMapId(nativeHero_);
        if (mapId_ == 0)
        {
            currentHero->Release();
            hero_ = nullptr;
            nativeHero_ = nullptr;
            mapName_.clear();
            return false;
        }
        const std::uint64_t scriptThingUid = hero_->GetUid();
        const std::uint64_t nativeThingUid = ReadNativeThingUid(nativeHero_);
        const game::Vector3 position = hero_->GetPosition();
        const float facing = ReadHeroFacing();

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
        game::hero_pawn::appearance::HeroAppearanceModifierState modifiers;
        const bool modifiersReady =
            game::hero_pawn::appearance::native::
                HeroAttachableAppearanceComponent::Capture(
                    nativeHero_, modifiers);
        if (scalesReady && morphSelfTest_ &&
            !game::hero_pawn::appearance::native::HeroMorphComponent::
                ApplyBoneScaleState(nativeHero_, heroBoneScales))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-owner-bone-scale-self-apply");
            ReleaseHero();
            return false;
        }
        if (scalesReady && morphSelfTest_)
        {
            diagnostics_.Event(
                "MultiplayerOwnerBoneScalesSelfApplied",
                "captured MassBoneScaling vectors were applied back to their source Hero");
        }

        {
            std::lock_guard<std::mutex> lock(ownerStateMutex_);
            channel_->Open(
                actorId_, 1, role_, playerId_, appearanceDefinition_,
                heroMorph, heroClothing, heroBoneScales, modifiers, mapName_,
                mapId_, position, facing, GetTickCount64());
        }
        PlayerState baseline;
        {
            std::lock_guard<std::mutex> lock(ownerStateMutex_);
            if (!channel_->TakeDirtyUpdate(baseline))
            {
                diagnostics_.Event("ClientFailed", "multiplayer-owner-baseline");
                ReleaseHero();
                return false;
            }
        }
        if (!transport_->Submit(baseline))
        {
            diagnostics_.Event("ClientFailed", "multiplayer-owner-baseline");
            ReleaseHero();
            return false;
        }

        worldReady_ = true;
        entryPending_ = false;
        // ReleaseHero detaches the observer during level teardown. Restore it
        // whenever a destination Hero reopens the owner channel.
        locomotion_->SetPlayerFrameObserver(
            &LocalHeroReplication::ObservePlayerFrame,
            this);
        appearanceReady_ = scalesReady && clothingReady && modifiersReady;
        transitionCompleted_ = transitionActive_;
        transitionActive_ = false;
        departingNativeHero_ = nullptr;
        departingMapName_.clear();

        char detail[448] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "player=%s actor_id=%llu local_presentation=selected-save-hero remote_definition=%s map=%s position=(%.3f,%.3f,%.3f) morph=(%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f) child=%s",
            playerId_.c_str(), static_cast<unsigned long long>(actorId_),
            appearanceDefinition_.c_str(), mapName_.c_str(), position.x,
            position.y, position.z, heroMorph.strength, heroMorph.berserk,
            heroMorph.will, heroMorph.skill, heroMorph.age,
            heroMorph.alignment, heroMorph.fatness,
            heroMorph.child ? "true" : "false");
        diagnostics_.Event("MultiplayerLocalHeroReady", detail);
        char identityDetail[256] = {};
        std::snprintf(
            identityDetail,
            sizeof(identityDetail),
            "script_uid=%llu native_uid=%llu match=%s map=%s native=%p",
            static_cast<unsigned long long>(scriptThingUid),
            static_cast<unsigned long long>(nativeThingUid),
            scriptThingUid != 0 && scriptThingUid == nativeThingUid
                ? "true"
                : "false",
            mapName_.c_str(), nativeHero_);
        diagnostics_.Event("MultiplayerLocalHeroThingIdentity", identityDetail);
        if (transitionCompleted_)
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
                scaleDetail, sizeof(scaleDetail),
                "count=%u index=%u reference_bone=%u scale=(%.6f,%.6f,%.6f)",
                heroBoneScales.count, static_cast<unsigned int>(index),
                entry.boneIndex, entry.x, entry.y, entry.z);
            diagnostics_.Event("MultiplayerLocalHeroBoneScale", scaleDetail);
        }
        diagnostics_.Event(
            "MultiplayerOwnerChannelOpened",
            scalesReady
                ? "selected-save Hero owns a bounded lifecycle-scoped replication channel with appearance"
                : "selected-save Hero owns identity, map, and movement; skeletal appearance is pending");
        diagnostics_.Event(
            "MultiplayerOwnerPresentationBound",
            appearanceReady_
                ? "selected-save Hero and optional morph presentation are ready"
                : "selected-save Hero actor channel is ready; optional morph translation is pending");
        return true;
    }

    void LocalHeroReplication::ObservePlayerFrame(
        void* context,
        void* playerCreature)
    {
        auto* const replication = static_cast<LocalHeroReplication*>(context);
        if (replication != nullptr)
        {
            replication->OnPlayerFrame(playerCreature);
        }
    }

    void LocalHeroReplication::OnPlayerFrame(void* playerCreature)
    {
        if (!initialized_)
        {
            return;
        }
        if (!worldReady_)
        {
            if (entryPending_ &&
                game::creature::native::CreatureFrameFunctions::
                    ValidatePlayerCreature(
                        entities_->GameModule(), playerCreature))
            {
                TryBind();
            }
            return;
        }
        if (!graphicRuntimeReported_)
        {
            graphicRuntimeReported_ =
                presentation::ReportHeroSkeletalPresentation(
                    "owner", nativeHero_, diagnostics_);
        }
        CaptureMovement(GetTickCount64());
        if (transport_->HasFailed() && !transportFailureReported_)
        {
            transportFailureReported_ = true;
            diagnostics_.Event("ClientFailed", "multiplayer-network-worker");
        }
    }

    void LocalHeroReplication::CaptureMovement(std::uint64_t now)
    {
        if (hero_ == nullptr || !hero_->IsValid())
        {
            return;
        }
        std::string mapName = hero_->GetCurrentMapName();
        if (mapName.empty())
        {
            mapName = mapName_;
        }
        PlayerState update;
        {
            std::lock_guard<std::mutex> lock(ownerStateMutex_);
            channel_->CaptureMovement(
                mapName, hero_->GetPosition(), ReadHeroFacing(), now);
            if (!channel_->TakeDirtyUpdate(update))
            {
                return;
            }
        }
        if (!transport_->Submit(update))
        {
            if (!transportFailureReported_)
            {
                transportFailureReported_ = true;
                diagnostics_.Event(
                    "ClientFailed", "multiplayer-owner-property-submit");
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

    float LocalHeroReplication::ReadHeroFacing() const noexcept
    {
        if (entities_ != nullptr && nativeHero_ != nullptr)
        {
            void* const navigator =
                game::entity::native::ThingComponentAccess::Find(
                    nativeHero_,
                    game::entity::native::ThingComponentType::
                        PhysicsNavigator);
            float facing = 0.0f;
            if (navigator != nullptr &&
                game::creature::look::native::CreatureLookFunctions::
                    ReadNavigatorFacing(
                        entities_->GameModule(), navigator, facing))
            {
                return facing;
            }
        }
        return hero_ != nullptr ? hero_->GetFacing() : 0.0f;
    }

    void LocalHeroReplication::CaptureAppearance(std::uint64_t now)
    {
        if (!worldReady_ || appearanceReady_ || nativeHero_ == nullptr ||
            now < nextAppearanceCaptureAt_)
        {
            return;
        }
        nextAppearanceCaptureAt_ = now + 500;
        game::hero_pawn::appearance::HeroMorphState morph;
        game::hero_pawn::appearance::HeroClothingState clothing;
        game::hero_pawn::appearance::HeroBoneScaleState boneScales;
        game::hero_pawn::appearance::HeroAppearanceModifierState modifiers;
        if (!game::hero_pawn::appearance::native::HeroMorphComponent::Capture(
                nativeHero_, morph) ||
            !game::hero_pawn::appearance::native::HeroMorphComponent::
                CaptureBoneScaleState(nativeHero_, boneScales) ||
            !game::hero_pawn::appearance::native::HeroClothingComponent::Capture(
                nativeHero_, clothing) ||
            !game::hero_pawn::appearance::native::
                HeroAttachableAppearanceComponent::Capture(
                    nativeHero_, modifiers))
        {
            if (now >= nextBindDiagnosticAt_)
            {
                nextBindDiagnosticAt_ = now + 5'000;
                game::hero_pawn::appearance::native::HeroMorphResolutionState
                    resolution;
                const bool resolved =
                    game::hero_pawn::appearance::native::HeroMorphComponent::
                        InspectResolution(nativeHero_, resolution);
                char detail[512] = {};
                std::snprintf(
                    detail, sizeof(detail),
                    "resolved=%s thing=%p hero_morph=%p graphic=%p graphic_vtable=%p bridge=%p bridge_vtable=%p pawn=%p skeletal_mesh=%p anim_tree=%p mass_bone_scaling=%p reference_bones=%d scales=%d",
                    resolved ? "true" : "false", resolution.thing,
                    resolution.heroMorphComponent, resolution.graphic,
                    resolution.graphicVtable, resolution.graphicBridge,
                    resolution.graphicBridgeVtable, resolution.pawn,
                    resolution.skeletalMeshComponent, resolution.animTree,
                    resolution.massBoneScaling, resolution.referenceBoneCount,
                    resolution.scaleCount);
                diagnostics_.Event(
                    "MultiplayerOwnerPresentationWaiting", detail);
            }
            return;
        }
        {
            std::lock_guard<std::mutex> lock(ownerStateMutex_);
            if (!channel_->CaptureAppearance(
                    appearanceDefinition_, morph, clothing, boneScales,
                    modifiers))
            {
                diagnostics_.Event(
                    "ClientFailed", "multiplayer-owner-appearance-capture");
                return;
            }
        }
        PlayerState update;
        {
            std::lock_guard<std::mutex> lock(ownerStateMutex_);
            if (!channel_->TakeDirtyUpdate(update))
            {
                diagnostics_.Event(
                    "ClientFailed", "multiplayer-owner-appearance-submit");
                return;
            }
        }
        if (!transport_->Submit(update))
        {
            diagnostics_.Event(
                "ClientFailed", "multiplayer-owner-appearance-submit");
            return;
        }
        appearanceReady_ = true;
        char detail[160] = {};
        std::snprintf(
            detail, sizeof(detail),
            "clothing=(%d,%d,%d,%d,%d,%d) bone_scale_count=%u modifier_count=%u source=selected-save-hero",
            clothing.definitionIndices[0], clothing.definitionIndices[1],
            clothing.definitionIndices[2], clothing.definitionIndices[3],
            clothing.definitionIndices[4], clothing.definitionIndices[5],
            boneScales.count, modifiers.count);
        diagnostics_.Event("MultiplayerOwnerAppearanceReplicated", detail);
    }

    bool LocalHeroReplication::WorldIsCurrent() const
    {
        if (hero_ == nullptr || !hero_->IsValid())
        {
            return false;
        }
        const std::string currentMap = hero_->GetCurrentMapName();
        return !currentMap.empty() && currentMap == mapName_;
    }

    void LocalHeroReplication::BeginWorldTransition() noexcept
    {
        if (!worldReady_ && !entryPending_)
        {
            return;
        }
        if (!transitionActive_)
        {
            transitionActive_ = true;
            departingNativeHero_ = nativeHero_;
            departingMapName_ = mapName_;
            diagnostics_.Event(
                "MultiplayerWorldTransitionStarted",
                "local selected-save Hero left its bound map; remote routing detached before UE3 level teardown");
        }
        ReleaseHero();
        entryPending_ = true;
    }

    bool LocalHeroReplication::ConsumeCompletedWorldTransition() noexcept
    {
        const bool completed = transitionCompleted_;
        transitionCompleted_ = false;
        return completed;
    }

    void LocalHeroReplication::ReleaseHero() noexcept
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
        entryPending_ = false;
        appearanceReady_ = false;
        nextBindDiagnosticAt_ = 0;
        nextAppearanceCaptureAt_ = 0;
        graphicRuntimeReported_ = false;
        mapName_.clear();
        mapId_ = 0;
    }

    void LocalHeroReplication::Shutdown() noexcept
    {
        ReleaseHero();
        entities_ = nullptr;
        locomotion_ = nullptr;
        channel_ = nullptr;
        transport_ = nullptr;
        diagnostics_ = {};
        role_ = PeerRole::Guest;
        actorId_ = 0;
        playerId_.clear();
        appearanceDefinition_.clear();
        departingNativeHero_ = nullptr;
        departingMapName_.clear();
        initialized_ = false;
        transitionActive_ = false;
        transitionCompleted_ = false;
        exchangeReported_ = false;
        transportFailureReported_ = false;
        morphSelfTest_ = false;
    }

    bool LocalHeroReplication::IsWorldReady() const noexcept
    {
        return worldReady_;
    }

    bool LocalHeroReplication::IsEntryPending() const noexcept
    {
        return entryPending_;
    }

    game::Entity* LocalHeroReplication::Hero() const noexcept
    {
        return hero_;
    }

    void* LocalHeroReplication::NativeHero() const noexcept
    {
        return nativeHero_;
    }

    const std::string& LocalHeroReplication::MapName() const noexcept
    {
        return mapName_;
    }

    std::uint16_t LocalHeroReplication::MapId() const noexcept
    {
        return mapId_;
    }

    const PlayerState* LocalHeroReplication::CurrentState() const noexcept
    {
        return channel_ != nullptr ? channel_->CurrentState() : nullptr;
    }
}
