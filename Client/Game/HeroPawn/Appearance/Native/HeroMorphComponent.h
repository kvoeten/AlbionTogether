#pragma once

#include "Game/HeroPawn/Appearance/HeroMorphState.h"

#include <array>
#include <cstdint>

namespace fable::game::hero_pawn::appearance::native
{
    struct HeroMorphResolutionState final
    {
        void* thing = nullptr;
        void* graphic = nullptr;
        void* graphicVtable = nullptr;
        void* graphicBridge = nullptr;
        void* graphicBridgeVtable = nullptr;
        void* pawn = nullptr;
        void* heroMorphComponent = nullptr;
        void* skeletalMeshComponent = nullptr;
        void* animTree = nullptr;
        void* massBoneScaling = nullptr;
        std::int32_t referenceBoneCount = 0;
        std::int32_t scaleCount = 0;
        std::int32_t morphTargetCount = 0;
        std::int32_t morphNodeCount = 0;
        std::int32_t weightedMorphNodeCount = 0;
        float maximumMorphNodeWeight = 0.0f;
        std::int32_t compositeTextureCommandCount = 0;
        std::uint32_t compositeTextureCommandHash = 0;
        std::array<std::uint32_t, 3> firstCompositeTextureCommand = {};
        float firstCompositeTextureWeight = 0.0f;
    };

    class HeroMorphComponent final
    {
    public:
        [[nodiscard]] static bool SetUpdateRequested(
            void* nativeThing,
            bool requested) noexcept;

        [[nodiscard]] static bool Capture(
            void* nativeThing,
            HeroMorphState& state) noexcept;

        [[nodiscard]] static bool CaptureBoneScaleState(
            void* nativeThing,
            HeroBoneScaleState& state) noexcept;

        [[nodiscard]] static bool InspectResolution(
            void* nativeThing,
            HeroMorphResolutionState& state) noexcept;

        // Stages owner-authored inputs without arming CTCHeroMorph's stock
        // lifecycle callback. Skeletal submission is handled separately.
        [[nodiscard]] static bool ApplyValues(
            void* nativeThing,
            const HeroMorphState& state) noexcept;

        // Resolves the proxy's own MassBoneScaling controller and applies the
        // named bone vectors through USkelControlMassScaling's native API.
        [[nodiscard]] static bool ApplyBoneScaleState(
            void* nativeThing,
            const HeroBoneScaleState& state,
            std::uint32_t* matchedCount = nullptr) noexcept;
    };
}
