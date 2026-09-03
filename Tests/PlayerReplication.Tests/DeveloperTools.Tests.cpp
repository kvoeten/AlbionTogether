#include "DeveloperTools/DeveloperToolBackend.h"

#include <cassert>

namespace
{
    using namespace fable::developer_tools;
    class FakeAdapter final : public IDeveloperToolAdapter
    {
    public:
        DeveloperToolResult SpawnEntity(const SpawnEntityCommand&) noexcept override { ++spawn; return Accepted(); }
        DeveloperToolResult TeleportEntity(const TeleportEntityCommand& c) noexcept override { ++teleport; auto r = Accepted(); r.entityUid = c.entityUid; return r; }
        DeveloperToolResult UseRegionExit(const UseRegionExitCommand& c) noexcept override { ++regionExit; auto r = Accepted(); r.entityUid = c.exitUid; return r; }
        DeveloperToolResult QueryQuest(const QuestCommand&) noexcept override { ++query; auto r = Accepted(); r.questKnown = true; return r; }
        DeveloperToolResult ActivateQuest(const ActivateQuestCommand&) noexcept override { ++activate; return Accepted(); }
        DeveloperToolResult QuerySaveSection(const SaveSectionCommand&) noexcept override { ++save; auto r = Accepted(); r.fingerprint = 99U; return r; }
        int spawn = 0, teleport = 0, regionExit = 0, query = 0, activate = 0, save = 0;
    private:
        static DeveloperToolResult Accepted() noexcept
        {
            DeveloperToolResult result;
            result.code = DeveloperToolResultCode::Accepted;
            return result;
        }
    };
}

int RunDeveloperToolsTests()
{
    using namespace fable::developer_tools;
    DeveloperToolBackend backend;
    assert(!backend.QueueSpawnEntity({}, {}));
    assert(!backend.QueueTeleportEntity(0U, {}));
    assert(!backend.QueueQuestQuery({}));
    assert(backend.QueueSpawnEntity(DeveloperToolText::From("GUARD"), {1.0F, 2.0F, 3.0F}));
    assert(backend.QueueTeleportEntity(7U, {4.0F, 5.0F, 6.0F}));
    assert(backend.QueueUseRegionExit(8U));
    assert(backend.QueueQuestQuery(DeveloperToolText::From("QUEST_ARENA")));
    assert(backend.QueueQuestActivation(DeveloperToolText::From("QUEST_ARENA")));
    assert(backend.QueueSaveSectionQuery(DeveloperSaveSection::Quests));
    FakeAdapter adapter;
    DeveloperToolResult results[6]{};
    assert(backend.ExecutePending(adapter, results, 6U) == 6U);
    assert(adapter.spawn == 1 && adapter.teleport == 1 && adapter.regionExit == 1 && adapter.query == 1);
    assert(adapter.activate == 1 && adapter.save == 1 && backend.PendingCount() == 0U);
    assert(results[0].command == DeveloperCommandKind::SpawnEntity);
    assert(results[1].command == DeveloperCommandKind::TeleportEntity);
    assert(results[2].command == DeveloperCommandKind::UseRegionExit);
    assert(results[3].command == DeveloperCommandKind::QueryQuest);
    assert(results[4].command == DeveloperCommandKind::ActivateQuest);
    assert(results[5].command == DeveloperCommandKind::QuerySaveSection);
    for (std::size_t i = 0U; i < backend.QueueCapacity(); ++i)
        assert(backend.QueueQuestQuery(DeveloperToolText::From("Q")));
    assert(!backend.QueueQuestQuery(DeveloperToolText::From("overflow")));
    return 0;
}
