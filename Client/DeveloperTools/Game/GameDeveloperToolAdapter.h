#pragma once

#include "../DeveloperToolBackend.h"

#include <atomic>

namespace fable::game
{
    class EntityService;
    class NpcService;
    class QuestService;
}

namespace fable::developer_tools
{
    // Persistence remains an optional adapter so this layer does not own save
    // parsing or native section hooks.
    class IDeveloperWorldAuthority
    {
    public:
        virtual ~IDeveloperWorldAuthority() = default;
        virtual bool ReadSaveSectionStatus(
            DeveloperSaveSection section,
            std::uint64_t& fingerprint,
            DeveloperToolText& detail) const noexcept = 0;
        virtual bool PublishQuestState() noexcept = 0;
    };

    class GameDeveloperToolAdapter final : public IDeveloperToolAdapter
    {
    public:
        GameDeveloperToolAdapter(
            game::EntityService* entities = nullptr,
            game::NpcService* npcs = nullptr,
            game::QuestService* quests = nullptr,
            IDeveloperWorldAuthority* worldAuthority = nullptr) noexcept;

        void Bind(
            game::EntityService* entities,
            game::NpcService* npcs,
            game::QuestService* quests,
            IDeveloperWorldAuthority* worldAuthority = nullptr) noexcept;

        void SetHostAuthorized(bool authorized) noexcept;
        void SetSessionIdentity(std::uint64_t sessionIdentity) noexcept;
        bool IsHostAuthorized() const noexcept;

        DeveloperToolResult SpawnEntity(const SpawnEntityCommand& command) noexcept override;
        DeveloperToolResult TeleportEntity(const TeleportEntityCommand& command) noexcept override;
        DeveloperToolResult UseRegionExit(const UseRegionExitCommand& command) noexcept override;
        DeveloperToolResult QueryQuest(const QuestCommand& command) noexcept override;
        DeveloperToolResult ActivateQuest(const ActivateQuestCommand& command) noexcept override;
        DeveloperToolResult QuerySaveSection(const SaveSectionCommand& command) noexcept override;

    private:
        DeveloperToolResult Rejected(DeveloperCommandKind command) const noexcept;

        game::EntityService* entities_ = nullptr;
        game::NpcService* npcs_ = nullptr;
        game::QuestService* quests_ = nullptr;
        IDeveloperWorldAuthority* worldAuthority_ = nullptr;
        std::atomic_bool hostAuthorized_{false};
        std::atomic_uint64_t sessionIdentity_{0};
        std::atomic_uint64_t nextSpawnSequence_{1};
    };
}
