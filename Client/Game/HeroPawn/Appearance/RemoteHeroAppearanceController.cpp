#include "RemoteHeroAppearanceController.h"

#include "Game/HeroPawn/Appearance/Diagnostics/HeroPresentationDiagnostics.h"
#include "Game/HeroPawn/Appearance/Native/HeroAttachableAppearanceComponent.h"
#include "Game/HeroPawn/Appearance/Native/HeroClothingComponent.h"
#include "Game/HeroPawn/Appearance/Native/HeroMorphComponent.h"

#include <Windows.h>

#include <cstdio>

namespace
{
    constexpr std::uint64_t kMutationFailureTimeoutMs = 5'000;
}

namespace fable::game::hero_pawn::appearance
{
    void RemoteHeroAppearanceController::Initialize(
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        diagnostics_ = diagnostics;
    }

    void RemoteHeroAppearanceController::Bind(
        void* nativeHero,
        std::uint64_t actorId) noexcept
    {
        Unbind();
        nativeHero_ = nativeHero;
        actorId_ = actorId;
    }

    bool RemoteHeroAppearanceController::StageInitial(
        const HeroMorphState& morph,
        bool appearanceReady)
    {
        if (nativeHero_ == nullptr)
        {
            return false;
        }
        if (appearanceReady &&
            !native::HeroMorphComponent::ApplyValues(nativeHero_, morph))
        {
            return false;
        }
        if (appearanceReady)
        {
            appliedMorph_ = morph;
        }
        diagnostics_.Event(
            "MultiplayerRemoteAppearancePending",
            appearanceReady
                ? "remote Hero-compatible creature appearance staged; waiting for its UE3 skeletal presentation"
                : "remote Hero-compatible creature uses its default presentation while appearance state is pending");
        return true;
    }

    RemoteHeroAppearanceResult RemoteHeroAppearanceController::Reconcile(
        const HeroMorphState& morph,
        const HeroClothingState& clothing,
        const HeroBoneScaleState& boneScales,
        const HeroAppearanceModifierState& modifiers,
        bool presentationRequired)
    {
        if (nativeHero_ == nullptr)
        {
            return RemoteHeroAppearanceResult::Failed;
        }
        const bool appearanceReady = morph.IsSane() && clothing.IsSane() &&
            boneScales.IsSane() && modifiers.IsSane();
        native::HeroMorphResolutionState resolution;
        const bool resolved = native::HeroMorphComponent::InspectResolution(
            nativeHero_, resolution);
        if (presentationRequired && !resolved)
        {
            if (!resolutionPendingReported_)
            {
                resolutionPendingReported_ = true;
                char detail[320] = {};
                std::snprintf(
                    detail,
                    sizeof(detail),
                    "actor_id=%llu native=%p graphic=%p bridge=%p pawn=%p skeletal_mesh=%p anim_tree=%p mass_bone_scaling=%p",
                    static_cast<unsigned long long>(actorId_),
                    nativeHero_,
                    resolution.graphic,
                    resolution.graphicBridge,
                    resolution.pawn,
                    resolution.skeletalMeshComponent,
                    resolution.animTree,
                    resolution.massBoneScaling);
                diagnostics_.Event(
                    "MultiplayerRemoteAppearanceResolutionPending", detail);
            }
            return RemoteHeroAppearanceResult::Pending;
        }
        resolutionPendingReported_ = false;
        if (!graphicRuntimeReported_)
        {
            graphicRuntimeReported_ = ReportHeroSkeletalPresentation(
                "remote", nativeHero_, diagnostics_);
        }
        if (!appearanceReady)
        {
            return RemoteHeroAppearanceResult::Ready;
        }
        if (!appliedClothing_.Equals(clothing))
        {
            std::uint32_t inserted = 0;
            if (!native::HeroClothingComponent::Apply(
                    nativeHero_, clothing, &inserted))
            {
                return MutationPending(
                    MutationStage::Clothing, "clothing");
            }
            MutationSucceeded();
            appliedClothing_ = clothing;
            char detail[224] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "selected=(%d,%d,%d,%d,%d,%d) inserted=%u operation=native-wear-rebuild",
                clothing.definitionIndices[0],
                clothing.definitionIndices[1],
                clothing.definitionIndices[2],
                clothing.definitionIndices[3],
                clothing.definitionIndices[4],
                clothing.definitionIndices[5],
                inserted);
            diagnostics_.Event("MultiplayerRemoteClothingApplied", detail);
            // Clothing rebuilds the promoted Hero's composite presentation.
            // Let the engine publish the resulting component graph before
            // applying another native appearance mutation.
            return RemoteHeroAppearanceResult::Pending;
        }
        if (!appliedModifiers_.Equals(modifiers))
        {
            std::uint32_t removed = 0;
            std::uint32_t added = 0;
            if (!native::HeroAttachableAppearanceComponent::Apply(
                    nativeHero_, modifiers, &removed, &added))
            {
                const RemoteHeroAppearanceResult pending = MutationPending(
                    MutationStage::Modifiers, "modifiers");
                if (pending != RemoteHeroAppearanceResult::Failed)
                {
                    return pending;
                }

                // Some retail Hero presentations reject attachable modifiers
                // even after their clothing graph is usable. Keeping the last
                // native modifier set is safer than destroying and respawning
                // the entire remote Hero every timeout interval. A fresh actor
                // incarnation or a changed modifier state will attempt the
                // mutation again.
                appliedModifiers_ = modifiers;
                MutationSucceeded();
                diagnostics_.Event(
                    "MultiplayerRemoteAppearanceModifiersDegraded",
                    "native modifier refresh was unavailable; retained the current actor presentation without rebuilding it");
                return RemoteHeroAppearanceResult::Pending;
            }
            MutationSucceeded();
            appliedModifiers_ = modifiers;
            char detail[192] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "source=%u removed=%u added=%u operation=component-refresh",
                modifiers.count,
                removed,
                added);
            diagnostics_.Event(
                "MultiplayerRemoteAppearanceModifiersApplied", detail);
            return RemoteHeroAppearanceResult::Pending;
        }
        if (!appliedMorph_.Equals(morph))
        {
            if (!native::HeroMorphComponent::ApplyValues(nativeHero_, morph))
            {
                return MutationPending(MutationStage::Morph, "morph");
            }
            MutationSucceeded();
            appliedMorph_ = morph;
            return RemoteHeroAppearanceResult::Pending;
        }
        if (!appliedBoneScales_.Equals(boneScales))
        {
            std::uint32_t matched = 0;
            if (!native::HeroMorphComponent::ApplyBoneScaleState(
                    nativeHero_, boneScales, &matched))
            {
                return MutationPending(
                    MutationStage::BoneScales, "bone-scales");
            }
            MutationSucceeded();
            appliedBoneScales_ = boneScales;
            char detail[160] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "matched=%u source=%u operation=refresh",
                matched,
                boneScales.count);
            diagnostics_.Event("MultiplayerRemoteBoneScalesApplied", detail);
        }
        return RemoteHeroAppearanceResult::Ready;
    }

    RemoteHeroAppearanceResult
        RemoteHeroAppearanceController::MutationPending(
            MutationStage stage,
            const char* stageName) noexcept
    {
        const std::uint64_t now = GetTickCount64();
        if (pendingMutationStage_ != stage)
        {
            pendingMutationStage_ = stage;
            mutationFailureStartedAt_ = now;
            mutationFailureCount_ = 0;
            mutationPendingReported_ = false;
        }
        ++mutationFailureCount_;
        if (!mutationPendingReported_)
        {
            mutationPendingReported_ = true;
            char detail[192] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "actor_id=%llu stage=%s operation=retry-after-native-rebuild",
                static_cast<unsigned long long>(actorId_),
                stageName != nullptr ? stageName : "unknown");
            diagnostics_.Event(
                "MultiplayerRemoteAppearanceMutationPending", detail);
        }
        if (now - mutationFailureStartedAt_ < kMutationFailureTimeoutMs)
        {
            return RemoteHeroAppearanceResult::Pending;
        }
        char detail[224] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu stage=%s attempts=%u timeout_ms=%llu",
            static_cast<unsigned long long>(actorId_),
            stageName != nullptr ? stageName : "unknown",
            mutationFailureCount_,
            static_cast<unsigned long long>(kMutationFailureTimeoutMs));
        diagnostics_.Event(
            "MultiplayerRemoteAppearanceMutationFailed", detail);
        return RemoteHeroAppearanceResult::Failed;
    }

    void RemoteHeroAppearanceController::MutationSucceeded() noexcept
    {
        pendingMutationStage_ = MutationStage::None;
        mutationFailureStartedAt_ = 0;
        mutationFailureCount_ = 0;
        mutationPendingReported_ = false;
    }

    void RemoteHeroAppearanceController::Unbind() noexcept
    {
        nativeHero_ = nullptr;
        actorId_ = 0;
        appliedMorph_ = {};
        appliedClothing_ = {};
        appliedBoneScales_ = {};
        appliedModifiers_ = {};
        MutationSucceeded();
        resolutionPendingReported_ = false;
        graphicRuntimeReported_ = false;
    }

    void RemoteHeroAppearanceController::Shutdown() noexcept
    {
        Unbind();
        diagnostics_ = {};
    }
}
