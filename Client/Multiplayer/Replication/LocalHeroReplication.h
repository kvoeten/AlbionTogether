#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/PlayerState.h"

#include <cstdint>
#include <mutex>
#include <string>

namespace fable::game
{
    class Entity;
    class EntityService;
}

namespace fable::game::creature::locomotion
{
    class CreatureLocomotionService;
}

namespace fable::multiplayer
{
    class UdpPeer;
}

namespace fable::multiplayer::replication
{
    class LocalPlayerChannel;

    // Owns the selected-save Hero binding and its owner-authored replication
    // channel. It does not consume remote actors or own their presentation.
    class LocalHeroReplication final
    {
    public:
        void Initialize(
            game::EntityService& entities,
            game::creature::locomotion::CreatureLocomotionService& locomotion,
            LocalPlayerChannel& channel,
            UdpPeer& transport,
            const core::Diagnostics& diagnostics,
            PeerRole role,
            std::uint64_t actorId,
            std::string playerId,
            std::string appearanceDefinition,
            bool morphSelfTest);
        bool OnWorldReady();
        bool TryBind();
        void CaptureAppearance(std::uint64_t now);
        [[nodiscard]] bool WorldIsCurrent() const;
        void BeginWorldTransition() noexcept;
        [[nodiscard]] bool ConsumeCompletedWorldTransition() noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool IsWorldReady() const noexcept;
        [[nodiscard]] bool IsEntryPending() const noexcept;
        [[nodiscard]] game::Entity* Hero() const noexcept;
        [[nodiscard]] void* NativeHero() const noexcept;
        [[nodiscard]] const std::string& MapName() const noexcept;
        [[nodiscard]] const PlayerState* CurrentState() const noexcept;

    private:
        static void ObservePlayerFrame(void* context, void* playerCreature);
        void OnPlayerFrame(void* playerCreature);
        void CaptureMovement(std::uint64_t now);
        [[nodiscard]] float ReadHeroFacing() const noexcept;
        void ReleaseHero() noexcept;

        game::EntityService* entities_ = nullptr;
        game::creature::locomotion::CreatureLocomotionService* locomotion_ =
            nullptr;
        LocalPlayerChannel* channel_ = nullptr;
        UdpPeer* transport_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::mutex ownerStateMutex_;
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t actorId_ = 0;
        std::string playerId_;
        std::string appearanceDefinition_;
        std::string mapName_;
        std::string departingMapName_;
        game::Entity* hero_ = nullptr;
        void* nativeHero_ = nullptr;
        void* departingNativeHero_ = nullptr;
        std::uint64_t nextBindDiagnosticAt_ = 0;
        std::uint64_t nextAppearanceCaptureAt_ = 0;
        bool initialized_ = false;
        bool worldReady_ = false;
        bool entryPending_ = false;
        bool appearanceReady_ = false;
        bool morphSelfTest_ = false;
        bool graphicRuntimeReported_ = false;
        bool transitionActive_ = false;
        bool transitionCompleted_ = false;
        bool exchangeReported_ = false;
        bool transportFailureReported_ = false;
    };
}
