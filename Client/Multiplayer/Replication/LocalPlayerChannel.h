#pragma once

#include "Multiplayer/Protocol/PlayerState.h"

#include <cstdint>
#include <string>

namespace fable::multiplayer::replication
{
    // One locally owned player actor channel. It retains current property
    // values and a dirty mask only; remote actors live in RemotePlayerChannels.
    class LocalPlayerChannel final
    {
    public:
        void Open(
            std::uint64_t actorId,
            std::uint32_t authorityEpoch,
            PeerRole role,
            const std::string& playerId,
            const std::string& appearanceDefinition,
            const game::hero_pawn::appearance::HeroMorphState& heroMorph,
            const game::hero_pawn::appearance::HeroClothingState& heroClothing,
            const game::hero_pawn::appearance::HeroBoneScaleState& heroBoneScales,
            const game::hero_pawn::appearance::HeroAppearanceModifierState&
                heroAppearanceModifiers,
            const std::string& mapName,
            std::uint16_t mapId,
            const game::Vector3& position,
            float facing,
            std::uint64_t capturedAtMilliseconds);
        bool CaptureMovement(
            const std::string& mapName,
            const game::Vector3& position,
            float facing,
            std::uint64_t capturedAtMilliseconds);
        bool CaptureAppearance(
            const std::string& appearanceDefinition,
            const game::hero_pawn::appearance::HeroMorphState& heroMorph,
            const game::hero_pawn::appearance::HeroClothingState& heroClothing,
            const game::hero_pawn::appearance::HeroBoneScaleState& heroBoneScales,
            const game::hero_pawn::appearance::HeroAppearanceModifierState&
                heroAppearanceModifiers);
        bool TakeDirtyUpdate(PlayerState& update);
        void Close() noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;
        [[nodiscard]] const PlayerState* CurrentState() const noexcept;

    private:
        PlayerState state_ = {};
        std::uint32_t dirtyProperties_ = 0;
        std::uint64_t lastCaptureAt_ = 0;
        bool open_ = false;
    };
}
