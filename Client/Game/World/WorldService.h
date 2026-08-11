#pragma once

#include "../../Core/Diagnostics/Diagnostics.h"
#include "../Math/Vector3.h"

#include <string>

namespace fable::game
{
    class Entity;
    class EntityService;

    class WorldService final
    {
    public:
        bool Initialize(EntityService& entities, const core::Diagnostics& diagnostics);

        [[nodiscard]] Entity* GetHero() const;
        [[nodiscard]] Entity* FindByScriptName(const std::string& scriptName) const;
        [[nodiscard]] Entity* CreateCreature(
            const std::string& definition,
            const Vector3& position,
            const std::string& scriptName) const;

    private:
        EntityService* entities_ = nullptr;
        core::Diagnostics diagnostics_ = {};
    };
}
