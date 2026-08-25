#include "RemoteHeroRangedAimController.h"

#include "Game/Creature/Locomotion/Hooks/CreatureModeManagerObserver.h"
#include "Game/Entity/Native/ThingComponentAccess.h"

#include <cstdio>

namespace fable::game::hero_pawn::combat
{
    void RemoteHeroRangedAimController::Initialize(
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        diagnostics_ = diagnostics;
    }

    void RemoteHeroRangedAimController::Bind(
        void* nativeHero,
        std::uint64_t actorId) noexcept
    {
        Unbind();
        nativeHero_ = nativeHero;
        actorId_ = actorId;
        modeManager_ = ResolveModeManager();
    }

    bool RemoteHeroRangedAimController::Begin() noexcept
    {
        if (active_)
        {
            return true;
        }
        if (modeManager_ == nullptr)
        {
            modeManager_ = ResolveModeManager();
        }
        if (modeManager_ == nullptr ||
            !creature::locomotion::CreatureModeManagerObserver::
                AddReplicatedSource(modeManager_, RangedAimModeSource))
        {
            return false;
        }
        active_ = true;
        char detail[160] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu hero=%p manager=%p source=%d",
            static_cast<unsigned long long>(actorId_),
            nativeHero_,
            modeManager_,
            RangedAimModeSource);
        diagnostics_.Event("MultiplayerRemoteRangedAimModeEntered", detail);
        return true;
    }

    bool RemoteHeroRangedAimController::End() noexcept
    {
        if (!active_)
        {
            return true;
        }
        const bool removed =
            creature::locomotion::CreatureModeManagerObserver::
                RemoveReplicatedSource(modeManager_, RangedAimModeSource);
        if (!removed)
        {
            return false;
        }
        active_ = false;
        char detail[160] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu hero=%p manager=%p source=%d",
            static_cast<unsigned long long>(actorId_),
            nativeHero_,
            modeManager_,
            RangedAimModeSource);
        diagnostics_.Event("MultiplayerRemoteRangedAimModeExited", detail);
        return true;
    }

    void RemoteHeroRangedAimController::Unbind() noexcept
    {
        if (active_)
        {
            (void)End();
        }
        nativeHero_ = nullptr;
        modeManager_ = nullptr;
        actorId_ = 0;
        active_ = false;
    }

    void RemoteHeroRangedAimController::Shutdown() noexcept
    {
        Unbind();
        diagnostics_ = {};
    }

    void* RemoteHeroRangedAimController::ResolveModeManager() const noexcept
    {
        return entity::native::ThingComponentAccess::Find(
            nativeHero_,
            entity::native::ThingComponentType::CreatureModeManager);
    }
}
