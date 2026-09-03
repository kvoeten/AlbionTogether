#pragma once

#include "DeveloperToolTypes.h"

#include <array>
#include <mutex>
#include <variant>

namespace fable::developer_tools
{
    using DeveloperCommand = std::variant<
        SpawnEntityCommand,
        TeleportEntityCommand,
        UseRegionExitCommand,
        QuestCommand,
        ActivateQuestCommand,
        SaveSectionCommand>;

    class DeveloperCommandQueue final
    {
    public:
        bool Enqueue(DeveloperCommand command) noexcept;
        bool TryDequeue(DeveloperCommand& command) noexcept;
        std::size_t Size() const noexcept;
        std::size_t Capacity() const noexcept;

    private:
        mutable std::mutex mutex_;
        std::array<DeveloperCommand, DeveloperToolQueueCapacity> entries_{};
        std::size_t readIndex_ = 0U;
        std::size_t writeIndex_ = 0U;
        std::size_t size_ = 0U;
    };
}
