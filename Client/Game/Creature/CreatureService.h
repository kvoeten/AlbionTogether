#pragma once

#include "../../Core/Diagnostics/Diagnostics.h"

namespace fable::game
{
    class Entity;
    class EntityService;

    class CreatureService final
    {
    public:
        bool Initialize(EntityService& entities, const core::Diagnostics& diagnostics);

        [[nodiscard]] bool IsCreature(Entity* entity) const;
        [[nodiscard]] float GetHealth(Entity* entity) const;
        [[nodiscard]] float GetMaximumHealth(Entity* entity) const;
        bool SetHealth(Entity* entity, float health);

    private:
        [[nodiscard]] void* ResolveCreature(Entity* entity) const;

        EntityService* entities_ = nullptr;
        core::Diagnostics diagnostics_ = {};
    };
}
