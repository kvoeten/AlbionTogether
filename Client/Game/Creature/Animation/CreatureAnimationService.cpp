#include "CreatureAnimationService.h"

#include "Game/Entity/EntityService.h"

#include <cstdio>

namespace fable::game::creature::animation
{
    bool CreatureAnimationService::Initialize(
        EntityService& entities,
        const core::Diagnostics& diagnostics)
    {
        diagnostics_ = diagnostics;
        playbackCount_.store(0, std::memory_order_release);
        const bool resolved = functions_.Resolve(entities.GameModule());
        diagnostics_.Event(
            "CreatureAnimationPlaybackReady",
            resolved
                ? "verified CTCAnimationComplex request construction and submission are available"
                : "current-build CTCAnimationComplex playback definitions failed validation");
        return resolved;
    }

    bool CreatureAnimationService::PlayAuthoritative(
        void* creature,
        std::uint32_t animationId,
        std::uint32_t flags) noexcept
    {
        const native::AnimationPlaybackAttempt attempt =
            functions_.Play(creature, animationId, flags);
        const bool played =
            attempt.result == native::AnimationPlaybackResult::Played;
        const unsigned int ordinal = playbackCount_.fetch_add(
            1, std::memory_order_acq_rel) + 1;
        if (ordinal <= DiagnosticEventLimit)
        {
            char detail[320] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "ordinal=%u creature=%p animation_id=%u flags=0x%X result=%s component=%p vtable=%p state=%p",
                ordinal,
                creature,
                animationId,
                flags,
                native::AnimationPlaybackFunctions::ResultName(
                    attempt.result),
                attempt.animationComplex,
                attempt.animationVtable,
                attempt.animationState);
            diagnostics_.Event("CreatureAnimationPlayback", detail);
        }
        return played;
    }

    bool CreatureAnimationService::IsReady() const noexcept
    {
        return functions_.IsResolved();
    }
}
