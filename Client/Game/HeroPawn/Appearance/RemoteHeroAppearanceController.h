#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/HeroPawn/Appearance/HeroAppearanceState.h"
#include "Game/HeroPawn/Appearance/HeroClothingState.h"
#include "Game/HeroPawn/Appearance/HeroMorphState.h"

#include <cstdint>

namespace fable::game::hero_pawn::appearance
{
    enum class RemoteHeroAppearanceResult
    {
        Ready,
        Pending,
        Failed,
    };

    // Actor-scoped Hero appearance reconciliation. It owns native morph,
    // clothing, bone-scale, and attachable-modifier application, independent
    // from networking and actor lifecycle.
    class RemoteHeroAppearanceController final
    {
    public:
        void Initialize(const core::Diagnostics& diagnostics) noexcept;
        void Bind(void* nativeHero, std::uint64_t actorId) noexcept;
        [[nodiscard]] bool StageInitial(
            const HeroMorphState& morph,
            bool appearanceReady);
        [[nodiscard]] RemoteHeroAppearanceResult Reconcile(
            const HeroMorphState& morph,
            const HeroClothingState& clothing,
            const HeroBoneScaleState& boneScales,
            const HeroAppearanceModifierState& modifiers,
            bool presentationRequired);
        void Unbind() noexcept;
        void Shutdown() noexcept;

    private:
        enum class MutationStage : std::uint8_t
        {
            None,
            Clothing,
            Modifiers,
            Morph,
            BoneScales,
        };

        [[nodiscard]] RemoteHeroAppearanceResult MutationPending(
            MutationStage stage,
            const char* stageName) noexcept;
        void MutationSucceeded() noexcept;

        void* nativeHero_ = nullptr;
        std::uint64_t actorId_ = 0;
        HeroMorphState appliedMorph_ = {};
        HeroClothingState appliedClothing_ = {};
        HeroBoneScaleState appliedBoneScales_ = {};
        HeroAppearanceModifierState appliedModifiers_ = {};
        core::Diagnostics diagnostics_ = {};
        MutationStage pendingMutationStage_ = MutationStage::None;
        std::uint64_t mutationFailureStartedAt_ = 0;
        std::uint32_t mutationFailureCount_ = 0;
        bool resolutionPendingReported_ = false;
        bool graphicRuntimeReported_ = false;
        bool mutationPendingReported_ = false;
    };
}
