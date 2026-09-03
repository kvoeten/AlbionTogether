#include "DeveloperToolsRuntime.h"

#include "DeveloperTools/Scripting/DeveloperToolBindings.h"

#include <array>

namespace fable::developer_tools::runtime
{
    DeveloperToolsRuntime::~DeveloperToolsRuntime()
    {
        Shutdown();
    }

    bool DeveloperToolsRuntime::Initialize(
        game::EntityService& entities,
        game::NpcService& npcs,
        game::QuestService& quests,
        IDeveloperWorldAuthority* const worldAuthority,
        const bool hostAuthorized,
        const std::uint64_t sessionIdentity) noexcept
    {
        Shutdown();
        adapter_.Bind(&entities, &npcs, &quests, worldAuthority);
        adapter_.SetHostAuthorized(hostAuthorized);
        adapter_.SetSessionIdentity(sessionIdentity);
        scripting::BindDeveloperToolApi(&backend_, hostAuthorized);
        initialized_ = true;
        return true;
    }

    void DeveloperToolsRuntime::Tick() noexcept
    {
        if (!initialized_) return;
        std::array<DeveloperToolResult, 8> results{};
        (void)backend_.ExecutePending(adapter_, results.data(), results.size());
    }

    void DeveloperToolsRuntime::Shutdown() noexcept
    {
        if (!initialized_) return;
        scripting::BindDeveloperToolApi(nullptr, false);
        adapter_.SetHostAuthorized(false);
        initialized_ = false;
    }

    bool DeveloperToolsRuntime::IsAvailable() const noexcept
    {
        return initialized_;
    }
}
