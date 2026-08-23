#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Creature/Equipment/Hooks/CreatureCarryingMutationObserver.h"
#include "Game/HeroPawn/Appearance/Hooks/HeroAppearanceMutationObserver.h"
#include "Game/HeroPawn/Equipment/Hooks/HeroEquipmentMutationObserver.h"
#include "Multiplayer/Protocol/PlayerState.h"

#include <atomic>
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

namespace fable::multiplayer::combat
{
    class PlayerCombatantDirectory;
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
            combat::PlayerCombatantDirectory& combatants,
            const core::Diagnostics& diagnostics,
            PeerRole role,
            std::uint64_t actorId,
            std::uint32_t authorityEpoch,
            std::string playerId,
            std::string appearanceDefinition,
            bool morphSelfTest);
        bool OnWorldReady();
        bool TryBind();
        void CaptureMovement(std::uint64_t now);
        void CaptureAppearance(std::uint64_t now);
        void CaptureEquipment(std::uint64_t now);
        [[nodiscard]] bool WorldIsCurrent() const;
        void BeginWorldTransition() noexcept;
        [[nodiscard]] bool ConsumeCompletedWorldTransition() noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool IsWorldReady() const noexcept;
        [[nodiscard]] bool IsEntryPending() const noexcept;
        [[nodiscard]] game::Entity* Hero() const noexcept;
        [[nodiscard]] void* NativeHero() const noexcept;
        [[nodiscard]] const std::string& MapName() const noexcept;
        [[nodiscard]] std::uint16_t MapId() const noexcept;
        [[nodiscard]] const PlayerState* CurrentState() const noexcept;

    private:
        static void ObservePlayerFrame(void* context, void* playerCreature);
        static void ObserveAppearanceMutation(
            void* context,
            const game::hero_pawn::appearance::hooks::
                HeroAppearanceMutationEvent& event);
        static void ObserveEquipmentMutation(void* context, void* component);
        static void ObserveCarryingMutation(
            void* context,
            const game::creature::equipment::hooks::
                CreatureCarryingMutationEvent& event);
        void OnPlayerFrame(void* playerCreature);
        void OnAppearanceMutation(
            const game::hero_pawn::appearance::hooks::
                HeroAppearanceMutationEvent& event) noexcept;
        void OnEquipmentMutation(void* component) noexcept;
        void OnCarryingMutation(
            const game::creature::equipment::hooks::
                CreatureCarryingMutationEvent& event) noexcept;
        [[nodiscard]] bool ReadHeroPosition(
            game::Vector3& position) const noexcept;
        [[nodiscard]] float ReadHeroFacing() const noexcept;
        void ReleaseHero() noexcept;

        game::EntityService* entities_ = nullptr;
        game::creature::locomotion::CreatureLocomotionService* locomotion_ =
            nullptr;
        LocalPlayerChannel* channel_ = nullptr;
        UdpPeer* transport_ = nullptr;
        combat::PlayerCombatantDirectory* combatants_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        game::hero_pawn::appearance::hooks::HeroAppearanceMutationObserver
            appearanceObserver_;
        game::hero_pawn::equipment::hooks::HeroEquipmentMutationObserver
            equipmentObserver_;
        game::creature::equipment::hooks::CreatureCarryingMutationObserver
            carryingObserver_;
        std::atomic_bool appearanceDirty_{false};
        std::atomic_bool equipmentDirty_{false};
        std::mutex ownerStateMutex_;
        PeerRole role_ = PeerRole::Guest;
        std::uint64_t actorId_ = 0;
        std::uint32_t authorityEpoch_ = 0;
        std::string playerId_;
        std::string appearanceDefinition_;
        std::string mapName_;
        std::uint16_t mapId_ = 0;
        std::string departingMapName_;
        game::Entity* hero_ = nullptr;
        void* nativeHero_ = nullptr;
        std::atomic<void*> nativeHeroForMutation_{nullptr};
        void* departingNativeHero_ = nullptr;
        std::uint64_t nextBindDiagnosticAt_ = 0;
        std::uint64_t nextAppearanceCaptureAt_ = 0;
        std::uint64_t nextEquipmentCaptureAt_ = 0;
        bool initialized_ = false;
        bool worldReady_ = false;
        bool entryPending_ = false;
        bool appearanceReady_ = false;
        bool equipmentReady_ = false;
        bool morphSelfTest_ = false;
        bool graphicRuntimeReported_ = false;
        bool transitionActive_ = false;
        bool transitionCompleted_ = false;
        bool exchangeReported_ = false;
        bool movingExchangeReported_ = false;
        bool transportFailureReported_ = false;
    };
}
