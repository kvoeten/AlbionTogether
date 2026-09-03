#include "DeveloperToolBackend.h"

#include <utility>

namespace fable::developer_tools
{
    bool DeveloperToolBackend::QueueSpawnEntity(
        DeveloperToolText definition,
        DeveloperToolPosition position) noexcept
    {
        if (definition.Empty())
        {
            return false;
        }
        return queue_.Enqueue(SpawnEntityCommand{definition, position});
    }

    bool DeveloperToolBackend::QueueTeleportEntity(
        std::uint64_t entityUid,
        DeveloperToolPosition position) noexcept
    {
        if (entityUid == 0U)
        {
            return false;
        }
        return queue_.Enqueue(TeleportEntityCommand{entityUid, position});
    }

    bool DeveloperToolBackend::QueueUseRegionExit(std::uint64_t exitUid) noexcept
    {
        if (exitUid == 0U)
        {
            return false;
        }
        return queue_.Enqueue(UseRegionExitCommand{exitUid});
    }

    bool DeveloperToolBackend::QueueQuestQuery(DeveloperToolText questName) noexcept
    {
        if (questName.Empty())
        {
            return false;
        }
        return queue_.Enqueue(QuestCommand{questName});
    }

    bool DeveloperToolBackend::QueueQuestActivation(DeveloperToolText questName) noexcept
    {
        if (questName.Empty())
        {
            return false;
        }
        return queue_.Enqueue(ActivateQuestCommand{questName});
    }

    bool DeveloperToolBackend::QueueSaveSectionQuery(DeveloperSaveSection section) noexcept
    {
        return queue_.Enqueue(SaveSectionCommand{section});
    }

    std::size_t DeveloperToolBackend::ExecutePending(
        IDeveloperToolAdapter& adapter,
        DeveloperToolResult* results,
        std::size_t resultCapacity) noexcept
    {
        if (results == nullptr || resultCapacity == 0U)
        {
            return 0U;
        }

        std::size_t executed = 0U;
        DeveloperCommand command;
        while (executed < resultCapacity && queue_.TryDequeue(command))
        {
            DeveloperToolResult result;
            if (const auto* spawn = std::get_if<SpawnEntityCommand>(&command))
            {
                result = adapter.SpawnEntity(*spawn);
                result.command = DeveloperCommandKind::SpawnEntity;
            }
            else if (const auto* teleport = std::get_if<TeleportEntityCommand>(&command))
            {
                result = adapter.TeleportEntity(*teleport);
                result.command = DeveloperCommandKind::TeleportEntity;
            }
            else if (const auto* exit = std::get_if<UseRegionExitCommand>(&command))
            {
                result = adapter.UseRegionExit(*exit);
                result.command = DeveloperCommandKind::UseRegionExit;
            }
            else if (const auto* quest = std::get_if<QuestCommand>(&command))
            {
                result = adapter.QueryQuest(*quest);
                result.command = DeveloperCommandKind::QueryQuest;
            }
            else if (const auto* activate = std::get_if<ActivateQuestCommand>(&command))
            {
                result = adapter.ActivateQuest(*activate);
                result.command = DeveloperCommandKind::ActivateQuest;
            }
            else if (const auto* save = std::get_if<SaveSectionCommand>(&command))
            {
                result = adapter.QuerySaveSection(*save);
                result.command = DeveloperCommandKind::QuerySaveSection;
            }
            results[executed++] = result;
            PublishResult(result);
        }
        return executed;
    }

    bool DeveloperToolBackend::TryTakeResult(
        DeveloperToolResult& result) noexcept
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        if (resultCount_ == 0U)
        {
            return false;
        }
        result = results_[resultReadIndex_];
        resultReadIndex_ = (resultReadIndex_ + 1U) % results_.size();
        --resultCount_;
        return true;
    }

    void DeveloperToolBackend::PublishResult(
        const DeveloperToolResult& result) noexcept
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        if (resultCount_ == results_.size())
        {
            resultReadIndex_ = (resultReadIndex_ + 1U) % results_.size();
            --resultCount_;
        }
        results_[resultWriteIndex_] = result;
        resultWriteIndex_ = (resultWriteIndex_ + 1U) % results_.size();
        ++resultCount_;
    }

    std::size_t DeveloperToolBackend::PendingCount() const noexcept
    {
        return queue_.Size();
    }

    std::size_t DeveloperToolBackend::QueueCapacity() const noexcept
    {
        return queue_.Capacity();
    }
}
