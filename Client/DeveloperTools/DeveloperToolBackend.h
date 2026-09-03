#pragma once

#include "DeveloperCommandQueue.h"

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace fable::developer_tools
{
    enum class DeveloperToolResultCode : std::uint8_t
    {
        Accepted,
        Rejected,
        Unavailable,
        QueueFull
    };

    struct DeveloperToolResult final
    {
        DeveloperCommandKind command = DeveloperCommandKind::QueryQuest;
        DeveloperToolResultCode code = DeveloperToolResultCode::Rejected;
        std::uint64_t entityUid = 0;
        bool questKnown = false;
        std::uint64_t fingerprint = 0;
        DeveloperToolText detail;
    };

    // Adapter boundary. Implementations belong to the game/service layer; the
    // developer command backend never performs native calls itself.
    class IDeveloperToolAdapter
    {
    public:
        virtual ~IDeveloperToolAdapter() = default;
        virtual DeveloperToolResult SpawnEntity(const SpawnEntityCommand& command) noexcept = 0;
        virtual DeveloperToolResult TeleportEntity(const TeleportEntityCommand& command) noexcept = 0;
        virtual DeveloperToolResult UseRegionExit(const UseRegionExitCommand& command) noexcept = 0;
        virtual DeveloperToolResult QueryQuest(const QuestCommand& command) noexcept = 0;
        virtual DeveloperToolResult ActivateQuest(const ActivateQuestCommand& command) noexcept = 0;
        virtual DeveloperToolResult QuerySaveSection(const SaveSectionCommand& command) noexcept = 0;
    };

    class DeveloperToolBackend final
    {
    public:
        bool QueueSpawnEntity(DeveloperToolText definition, DeveloperToolPosition position) noexcept;
        bool QueueTeleportEntity(std::uint64_t entityUid, DeveloperToolPosition position) noexcept;
        bool QueueUseRegionExit(std::uint64_t exitUid) noexcept;
        bool QueueQuestQuery(DeveloperToolText questName) noexcept;
        bool QueueQuestActivation(DeveloperToolText questName) noexcept;
        bool QueueSaveSectionQuery(DeveloperSaveSection section) noexcept;

        std::size_t ExecutePending(IDeveloperToolAdapter& adapter,
            DeveloperToolResult* results,
            std::size_t resultCapacity) noexcept;
        bool TryTakeResult(DeveloperToolResult& result) noexcept;
        std::size_t PendingCount() const noexcept;
        std::size_t QueueCapacity() const noexcept;

    private:
        void PublishResult(const DeveloperToolResult& result) noexcept;

        DeveloperCommandQueue queue_;
        mutable std::mutex resultMutex_;
        std::array<DeveloperToolResult, DeveloperToolQueueCapacity> results_{};
        std::size_t resultReadIndex_ = 0U;
        std::size_t resultWriteIndex_ = 0U;
        std::size_t resultCount_ = 0U;
    };
}
