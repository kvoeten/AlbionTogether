#include "PlayerService.h"

#include "../Creature/CreatureService.h"
#include "../Entity/Entity.h"
#include "../Entity/EntityService.h"

namespace fable::game
{
    bool PlayerService::Initialize(
        EntityService& entities,
        CreatureService& creatures,
        const core::Diagnostics& diagnostics)
    {
        entities_ = &entities;
        creatures_ = &creatures;
        diagnostics_ = diagnostics;
        return true;
    }

    Entity* PlayerService::GetHero() const
    {
        return entities_ != nullptr ? entities_->GetHero() : nullptr;
    }

    float PlayerService::GetHealth() const
    {
        Entity* const hero = GetHero();
        if (hero == nullptr)
        {
            return -1.0f;
        }
        const float health = creatures_->GetHealth(hero);
        hero->Release();
        return health;
    }

    float PlayerService::GetMaximumHealth() const
    {
        Entity* const hero = GetHero();
        if (hero == nullptr)
        {
            return -1.0f;
        }
        const float maximum = creatures_->GetMaximumHealth(hero);
        hero->Release();
        return maximum;
    }

    bool PlayerService::SetHealth(float health)
    {
        Entity* const hero = GetHero();
        if (hero == nullptr)
        {
            return false;
        }
        const bool applied = creatures_->SetHealth(hero, health);
        hero->Release();
        if (!applied)
        {
            diagnostics_.Log("Player API: Hero combat-health mutation failed.");
        }
        return applied;
    }
}
