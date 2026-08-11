#pragma once

#include "../../Core/Diagnostics/Diagnostics.h"

#include <string>

namespace fable::game
{
    class EntityService;

    class QuestService final
    {
    public:
        bool Initialize(EntityService& entities, const core::Diagnostics& diagnostics);

        [[nodiscard]] bool IsActive(const std::string& questName) const;
        [[nodiscard]] bool IsRegistered(const std::string& questName) const;
        [[nodiscard]] bool IsCompleted(const std::string& questName) const;
        [[nodiscard]] bool IsFailed(const std::string& questName) const;

    private:
        [[nodiscard]] bool Query(const std::string& questName, std::size_t vtableIndex) const;

        EntityService* entities_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        mutable bool apiValidated_ = false;
    };
}
