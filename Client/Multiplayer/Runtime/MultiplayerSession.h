#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Look/Hooks/CreatureFacingInputRouterHook.h"
#include "Game/HeroPawn/Appearance/Hooks/RemoteHeroPresentationFactoryHook.h"
#include "Game/Math/Vector3.h"
#include "Multiplayer/Protocol/PlayerState.h"
#include "Multiplayer/Replication/PlayerActorChannel.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace fable::automation::runtime
{
    class RuntimeConfiguration;
}

namespace fable::game
{
    class Entity;
    class EntityService;
    class NpcService;
    class ScriptControl;
}

namespace fable::game::creature::locomotion
{
    class CreatureLocomotionService;
}

namespace fable::game::creature::look
{
    class CreatureLookService;
}

namespace fable::multiplayer
{
    class MultiplayerSession final
    {
    public:
        MultiplayerSession() = default;
        ~MultiplayerSession();

        MultiplayerSession(const MultiplayerSession&) = delete;
        MultiplayerSession& operator=(const MultiplayerSession&) = delete;

        bool Initialize(
            const automation::runtime::RuntimeConfiguration& configuration,
            game::EntityService& entities,
            game::NpcService& npcs,
            game::creature::locomotion::CreatureLocomotionService& locomotion,
            game::creature::look::CreatureLookService& look,
            const core::Diagnostics& diagnostics);
        bool OnWorldReady();
        // Returns true when the currently bound UE3 world started unloading.
        bool ProcessPresentationLifecycle();
        void Shutdown() noexcept;

        [[nodiscard]] bool IsEnabled() const noexcept;
        [[nodiscard]] bool IsWorldReady() const noexcept;

    private:
        bool BindLocalHero();
        bool SpawnRemoteAvatar(const PlayerState& state);
        void SuspendRemoteAvatar(const PlayerState& state) noexcept;
        bool ResumeRemoteAvatar(const PlayerState& state);
        void RetireRemoteAvatar(bool worldUnloading = false) noexcept;
        void ReleaseDeferredWorldPresentations() noexcept;
        void ReleaseLocalHero() noexcept;
        void BeginWorldTransition() noexcept;
        [[nodiscard]] bool LocalWorldIsCurrent() const;
        void OnPlayerFrame(void* playerCreature);
        void CaptureAndReplicateOwnerState(std::uint64_t now);
        void CaptureAndReplicateOwnerAppearance(std::uint64_t now);
        void ReconcileRemotePresentation(const PlayerState& state);
        bool ProvideRemoteMovement(
            void* creature,
            game::creature::look::CreatureFacingInputRouterHook::ReplicatedMovementInput& input);
        void ObserveRemoteLocomotion();
        static void ObservePlayerFrame(
            void* context,
            void* playerCreature);
        static bool ReadReplicatedMovement(
            void* context,
            void* creature,
            game::creature::look::CreatureFacingInputRouterHook::ReplicatedMovementInput& input);

        game::EntityService* entities_ = nullptr;
        game::NpcService* npcs_ = nullptr;
        game::creature::locomotion::CreatureLocomotionService* locomotion_ = nullptr;
        game::creature::look::CreatureLookService* look_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        game::hero_pawn::appearance::hooks::RemoteHeroPresentationFactoryHook
            remoteHeroPresentationFactory_;
        UdpPeer transport_;
        replication::PlayerActorChannel playerChannel_;
        std::mutex ownerStateMutex_;
        std::mutex remoteStateMutex_;
        PeerRole role_ = PeerRole::Guest;
        std::string playerId_;
        std::string appearanceDefinition_;
        game::Entity* hero_ = nullptr;
        void* nativeHero_ = nullptr;
        void* departingNativeHero_ = nullptr;
        game::Entity* remoteAvatar_ = nullptr;
        void* nativeRemoteAvatar_ = nullptr;
        game::ScriptControl* remoteControl_ = nullptr;
        struct DeferredWorldPresentation final
        {
            game::Entity* avatar = nullptr;
            game::ScriptControl* control = nullptr;
        };
        // A session can have only one departing world because it cannot bind a
        // second transition until the destination Hero is established.
        DeferredWorldPresentation deferredWorldPresentation_;
        std::string remotePlayerId_;
        std::string remoteAppearanceDefinition_;
        game::hero_pawn::appearance::HeroMorphState remoteAppliedMorph_ = {};
        game::hero_pawn::appearance::HeroClothingState remoteAppliedClothing_ = {};
        game::hero_pawn::appearance::HeroBoneScaleState remoteAppliedBoneScales_ = {};
        game::hero_pawn::appearance::HeroAppearanceModifierState
            remoteAppliedAppearanceModifiers_ = {};
        std::string localMapName_;
        game::Vector3 remoteLocomotionStartPosition_ = {};
        std::uint32_t remoteLocomotionStartAnimationHash_ = 0;
        std::uint64_t localActorId_ = 0;
        std::uint64_t nextRemoteSpawnAttemptAt_ = 0;
        std::uint64_t nextOwnerBindDiagnosticAt_ = 0;
        std::uint64_t nextOwnerAppearanceCaptureAt_ = 0;
        std::atomic_uint64_t remoteSampleReceivedAt_{0};
        std::uint64_t lastRemoteObservationAt_ = 0;
        bool enabled_ = false;
        bool worldReady_ = false;
        bool worldEntryPending_ = false;
        bool ownerAppearanceReady_ = false;
        bool remoteMovementCommanded_ = false;
        bool remoteWalkingReported_ = false;
        bool remoteSeparationReported_ = false;
        bool remoteStateAppliedReported_ = false;
        bool exchangeReported_ = false;
        bool transportFailureReported_ = false;
        bool morphSelfTest_ = false;
        bool ownerGraphicRuntimeReported_ = false;
        bool remoteGraphicRuntimeReported_ = false;
        bool worldTransitionReported_ = false;
        bool remoteAvatarSuspended_ = false;
    };
}
