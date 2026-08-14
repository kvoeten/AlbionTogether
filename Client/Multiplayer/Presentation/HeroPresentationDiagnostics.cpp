#include "HeroPresentationDiagnostics.h"

#include "Game/HeroPawn/Appearance/Native/HeroMorphComponent.h"

#include <cstdio>

namespace fable::multiplayer::presentation
{
    bool ReportHeroSkeletalPresentation(
        const char* label,
        void* nativeThing,
        const core::Diagnostics& diagnostics)
    {
        game::hero_pawn::appearance::native::HeroMorphResolutionState state;
        const bool resolved =
            game::hero_pawn::appearance::native::HeroMorphComponent::
                InspectResolution(nativeThing, state);
        char detail[640] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "%s resolved=%s thing=%p hero_morph=%p graphic=%p graphic_vtable=%p bridge=%p bridge_vtable=%p pawn=%p skeletal_mesh=%p anim_tree=%p mass_bone_scaling=%p reference_bones=%d scales=%d morph_targets=%d morph_nodes=%d weighted_morph_nodes=%d max_morph_weight=%.4f composite_commands=%d composite_hash=%08X first_composite=(%08X,%08X,%08X,%.4f)",
            label,
            resolved ? "true" : "false",
            state.thing,
            state.heroMorphComponent,
            state.graphic,
            state.graphicVtable,
            state.graphicBridge,
            state.graphicBridgeVtable,
            state.pawn,
            state.skeletalMeshComponent,
            state.animTree,
            state.massBoneScaling,
            state.referenceBoneCount,
            state.scaleCount,
            state.morphTargetCount,
            state.morphNodeCount,
            state.weightedMorphNodeCount,
            state.maximumMorphNodeWeight,
            state.compositeTextureCommandCount,
            state.compositeTextureCommandHash,
            state.firstCompositeTextureCommand[0],
            state.firstCompositeTextureCommand[1],
            state.firstCompositeTextureCommand[2],
            state.firstCompositeTextureWeight);
        diagnostics.Event("MultiplayerHeroSkeletalPresentation", detail);
        return resolved;
    }
}
