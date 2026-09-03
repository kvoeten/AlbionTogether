#pragma once

#include "../Game/GameDeveloperToolAdapter.h"

namespace fable::developer_tools::runtime
{
    class DeveloperToolsRuntime final
    {
    public:
        DeveloperToolsRuntime() = default;
        ~DeveloperToolsRuntime();

        DeveloperToolsRuntime(const DeveloperToolsRuntime&) = delete;
        DeveloperToolsRuntime& operator=(const DeveloperToolsRuntime&) = delete;

        bool Initialize(
            game::EntityService& entities,
            game::NpcService& npcs,
            game::QuestService& quests,
            IDeveloperWorldAuthority* worldAuthority,
            bool hostAuthorized,
            std::uint64_t sessionIdentity) noexcept;
        void Tick() noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool IsAvailable() const noexcept;

    private:
        DeveloperToolBackend backend_;
        GameDeveloperToolAdapter adapter_;
        bool initialized_ = false;
    };
}
