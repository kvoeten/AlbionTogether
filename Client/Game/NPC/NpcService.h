#pragma once

#include "../../Core/Diagnostics/Diagnostics.h"
#include "../Entity/Entity.h"

#include <string>
#include <cstddef>

namespace fable::game
{
    class EntityService;
    class ScriptControl;

    class NpcService final
    {
    public:
        bool Initialize(EntityService& entities, const core::Diagnostics& diagnostics);

        [[nodiscard]] Entity* Spawn(
            const std::string& definition,
            const Vector3& position,
            const std::string& scriptName);
        [[nodiscard]] ScriptControl* TakeControl(
            Entity* npc,
            AiPriority priority = AiPriority::Highest);

    private:
        void LogComponentStack(Entity* npc, const std::string& definition);

        EntityService* entities_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::size_t componentStacksLogged_ = 0;
    };
}
