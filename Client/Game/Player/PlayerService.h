#pragma once

#include "../../Core/Diagnostics/Diagnostics.h"

namespace fable::game
{
    class CreatureService;
    class Entity;
    class EntityService;

    class PlayerService final
    {
    public:
        bool Initialize(
            EntityService& entities,
            CreatureService& creatures,
            const core::Diagnostics& diagnostics);

        [[nodiscard]] Entity* GetHero() const;
        [[nodiscard]] float GetHealth() const;
        [[nodiscard]] float GetMaximumHealth() const;
        bool SetHealth(float health);

    private:
        EntityService* entities_ = nullptr;
        CreatureService* creatures_ = nullptr;
        core::Diagnostics diagnostics_ = {};
    };
}
