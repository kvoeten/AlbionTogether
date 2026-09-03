#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Multiplayer/Protocol/PlayerState.h"

#include <cstdint>

namespace fable::game
{
    class EntityService;
}

namespace fable::multiplayer::replication
{
    enum class LocalHeroRestoreResult : std::uint8_t
    {
        Ready,
        Pending,
        Failed,
    };

    // Preserves only player-owned Hero presentation and equipped-weapon
    // state while the host's map record is installed. World/NPC state stays
    // host-authored; the destination map cannot replace the guest's Hero.
    class LocalHeroOwnedStateRestorer final
    {
    public:
        void Preserve(
            const PlayerState* state,
            const core::Diagnostics& diagnostics) noexcept;
        [[nodiscard]] LocalHeroRestoreResult Reconcile(
            game::EntityService& entities,
            void* nativeHero,
            const game::hero_pawn::appearance::HeroMorphState& morph,
            const game::hero_pawn::appearance::HeroClothingState& clothing,
            const game::hero_pawn::appearance::HeroBoneScaleState& boneScales,
            const game::hero_pawn::appearance::HeroAppearanceModifierState&
                modifiers,
            const game::hero_pawn::equipment::HeroEquipmentState& equipment,
            const core::Diagnostics& diagnostics) noexcept;
        [[nodiscard]] bool HasPendingState() const noexcept;
        [[nodiscard]] const PlayerState* PreservedState() const noexcept;
        void Clear() noexcept;

    private:
        PlayerState preserved_;
        std::uint64_t nextAttemptAt_ = 0;
        std::uint64_t definitionsRequestedAt_ = 0;
        std::uint8_t lastFailedStage_ = 0;
        bool pending_ = false;
        bool definitionsRequested_ = false;
        bool modifiersDegraded_ = false;
    };
}
