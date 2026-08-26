#include "HeroPawnService.h"

#include "../Entity/Entity.h"
#include "../Entity/EntityService.h"

namespace fable::game
{
    bool HeroPawnService::Initialize(
        EntityService& entities,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        entities_ = &entities;
        diagnostics_ = diagnostics;
        if (entities.GameModule() == nullptr ||
            !mapTransitionUiActionSafetyHook_.Install(
                entities.GameModule(), diagnostics_) ||
            !guildTeleportSafetyHook_.Install(
                entities.GameModule(), diagnostics_))
        {
            Shutdown();
            return false;
        }
        return true;
    }

    void HeroPawnService::Shutdown() noexcept
    {
        guildTeleportSafetyHook_.Shutdown();
        mapTransitionUiActionSafetyHook_.Shutdown();
        entities_ = nullptr;
        diagnostics_ = {};
    }

    Entity* HeroPawnService::Get() const
    {
        return entities_ != nullptr ? entities_->GetHero() : nullptr;
    }

    bool HeroPawnService::SetVisible(Entity* hero, bool visible)
    {
        if (hero == nullptr)
        {
            return false;
        }
        const bool applied = hero->SetDrawable(visible);
        if (!applied)
        {
            diagnostics_.Log("HeroPawn API: drawable state change failed.");
        }
        return applied;
    }
}
