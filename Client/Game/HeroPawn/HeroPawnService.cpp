#include "HeroPawnService.h"

#include "../Entity/Entity.h"
#include "../Entity/EntityService.h"

namespace fable::game
{
    bool HeroPawnService::Initialize(
        EntityService& entities,
        const core::Diagnostics& diagnostics)
    {
        entities_ = &entities;
        diagnostics_ = diagnostics;
        return entities.GameModule() != nullptr;
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
