#include "CreatureAnimationService.h"

#include "Game/Entity/EntityService.h"

#include <cstdio>

namespace fable::game::creature::animation
{
    CreatureAnimationService::~CreatureAnimationService()
    {
        Shutdown();
    }

    bool CreatureAnimationService::Initialize(
        EntityService& entities,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        diagnostics_ = diagnostics;
        playbackCount_.store(0, std::memory_order_release);
        const bool playbackResolved = functions_.Resolve(
            entities.GameModule());
        const bool selectionInstalled = actionSelectionHook_.Install(
            entities.GameModule(), diagnostics);
        diagnostics_.Event(
            "CreatureAnimationPlaybackReady",
            playbackResolved && selectionInstalled
                ? "verified playback and replicated native-action animation selection are available"
                : "current-build animation playback or action-selection definitions failed validation");
        if (!playbackResolved || !selectionInstalled)
        {
            Shutdown();
            return false;
        }
        return true;
    }

    void CreatureAnimationService::Shutdown() noexcept
    {
        actionSelectionHook_.Shutdown();
        (void)functions_.Resolve(nullptr);
        playbackCount_.store(0, std::memory_order_release);
        diagnostics_ = {};
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
        return functions_.IsResolved() && actionSelectionHook_.IsInstalled();
    }

    bool CreatureAnimationService::BeginReplicatedActionSelection(
        void* creature,
        const char* actionType,
        const std::uint32_t animationId) noexcept
    {
        return actionSelectionHook_.BeginSelection(
            creature, actionType, animationId);
    }

    void CreatureAnimationService::EndReplicatedActionSelection() noexcept
    {
        actionSelectionHook_.EndSelection();
    }

    bool CreatureAnimationService::AttachActionLifecycleObserver(
        actions::CreatureActionLifecycleObserver& observer) noexcept
    {
        return actionSelectionHook_.AttachActionLifecycleObserver(observer);
    }

    void CreatureAnimationService::DetachActionLifecycleObserver() noexcept
    {
        actionSelectionHook_.DetachActionLifecycleObserver();
    }
}
