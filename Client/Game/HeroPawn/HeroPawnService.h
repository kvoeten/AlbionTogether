#pragma once

#include "../../Core/Diagnostics/Diagnostics.h"

namespace fable::game
{
    class Entity;
    class EntityService;

    class HeroPawnService final
    {
    public:
        bool Initialize(EntityService& entities, const core::Diagnostics& diagnostics);

        [[nodiscard]] Entity* Get() const;
        bool SetVisible(Entity* hero, bool visible);

    private:
        EntityService* entities_ = nullptr;
        core::Diagnostics diagnostics_ = {};
    };
}
