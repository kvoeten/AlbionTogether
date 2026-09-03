#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/HeroPawn/Appearance/HeroClothingState.h"
#include "Game/HeroPawn/Equipment/HeroEquipmentState.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game
{
    class EntityService;
}

namespace fable::multiplayer
{
    class MultiplayerRuntimeGraph;
}

namespace fable::automation::multiplayer::transition
{
    class MapStressAcceptanceDriver;

    // Host-owned mutation and observer-side assertions for the map-stress
    // scenario. This exercises only existing native-safe clothing/equipment
    // APIs and validates current actor state, never production replication.
    class OwnerScopedPresentationAcceptanceDriver final
    {
    public:
        void Initialize(
            bool enabled,
            bool host,
            game::EntityService& entities,
            ::fable::multiplayer::MultiplayerRuntimeGraph& multiplayer,
            const MapStressAcceptanceDriver& mapStress,
            const core::Diagnostics& diagnostics) noexcept;
        void Tick() noexcept;
        void Shutdown() noexcept;

    private:
        struct ActorBaseline final
        {
            std::uint64_t actorId = 0;
            game::hero_pawn::appearance::HeroClothingState clothing;
            game::hero_pawn::equipment::HeroEquipmentState equipment;
            game::hero_pawn::appearance::HeroClothingState expectedClothing;
            game::hero_pawn::equipment::HeroEquipmentState expectedEquipment;
            bool captured = false;
            bool expectedCaptured = false;
        };

        [[nodiscard]] bool CaptureLocalBaseline() noexcept;
        [[nodiscard]] bool ApplyOwnerMutation() noexcept;
        [[nodiscard]] bool ValidateCheckpoint() noexcept;
        [[nodiscard]] bool ReadLocalState(
            game::hero_pawn::appearance::HeroClothingState& clothing,
            game::hero_pawn::equipment::HeroEquipmentState& equipment) const
            noexcept;
        [[nodiscard]] bool ReadRemoteBaselines() noexcept;
        void Fail(const char* reason) noexcept;
        void Report(const char* event, const char* detail) const noexcept;

        static constexpr std::size_t MaxActors = 8;
        static constexpr std::uint64_t MutationRetryMilliseconds = 250;
        static constexpr unsigned int RequiredStableCheckpoints = 2;

        game::EntityService* entities_ = nullptr;
        ::fable::multiplayer::MultiplayerRuntimeGraph* multiplayer_ = nullptr;
        const MapStressAcceptanceDriver* mapStress_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::array<ActorBaseline, MaxActors> baselines_;
        game::hero_pawn::appearance::HeroClothingState ownerClothingBefore_;
        game::hero_pawn::appearance::HeroClothingState ownerClothingAfter_;
        game::hero_pawn::equipment::HeroEquipmentState ownerEquipmentBefore_;
        game::hero_pawn::equipment::HeroEquipmentState ownerEquipmentAfter_;
        std::uint64_t nextMutationAttemptAt_ = 0;
        std::uint64_t mutationObservedAt_ = 0;
        unsigned int lastValidatedTransitionOrdinal_ =
            static_cast<unsigned int>(-1);
        unsigned int stableCheckpointCount_ = 0;
        bool host_ = false;
        bool enabled_ = false;
        bool ownerMutationApplied_ = false;
        bool ownerMutationObserved_ = false;
        bool completed_ = false;
        bool failed_ = false;
    };
}
