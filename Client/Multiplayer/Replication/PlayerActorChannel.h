#pragma once

#include "Multiplayer/Protocol/PlayerState.h"

#include <cstdint>
#include <string>

namespace fable::multiplayer::replication
{
    // One lifecycle-scoped player channel. It retains current property values
    // and a dirty mask only; it never accumulates snapshot history.
    class PlayerActorChannel final
    {
    public:
        void OpenOwner(
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
            const game::Vector3& position,
            float facing,
            std::uint64_t capturedAtMilliseconds);
        bool CaptureOwnerMovement(
            const std::string& mapName,
            const game::Vector3& position,
            float facing,
            std::uint64_t capturedAtMilliseconds);
        bool CaptureOwnerAppearance(
            const std::string& appearanceDefinition,
            const game::hero_pawn::appearance::HeroMorphState& heroMorph,
            const game::hero_pawn::appearance::HeroClothingState& heroClothing,
            const game::hero_pawn::appearance::HeroBoneScaleState& heroBoneScales,
            const game::hero_pawn::appearance::HeroAppearanceModifierState&
                heroAppearanceModifiers);
        bool TakeDirtyUpdate(PlayerState& update);

        bool ApplyRemoteUpdate(const PlayerState& update);
        [[nodiscard]] const PlayerState* RemoteState() const noexcept;

        void Close() noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;

    private:
        PlayerState ownerState_ = {};
        PlayerState remoteState_ = {};
        std::uint32_t dirtyProperties_ = 0;
        std::uint64_t lastOwnerCaptureAt_ = 0;
        bool ownerOpen_ = false;
        bool remoteOpen_ = false;
    };
}
