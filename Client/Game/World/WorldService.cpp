#include "WorldService.h"

#include "../Entity/EntityService.h"

namespace fable::game
{
    bool WorldService::Initialize(
        EntityService& entities,
        const core::Diagnostics& diagnostics)
    {
        entities_ = &entities;
        diagnostics_ = diagnostics;
        return entities.GameModule() != nullptr;
    }

    Entity* WorldService::GetHero() const
    {
        return entities_ != nullptr ? entities_->GetHero() : nullptr;
    }

    Entity* WorldService::FindByScriptName(const std::string& scriptName) const
    {
        return entities_ != nullptr
            ? entities_->FindByScriptName(scriptName)
            : nullptr;
    }

    Entity* WorldService::CreateCreature(
        const std::string& definition,
        const Vector3& position,
        const std::string& scriptName) const
    {
        return entities_ != nullptr
            ? entities_->CreateCreature(definition, position, scriptName)
            : nullptr;
    }
}
