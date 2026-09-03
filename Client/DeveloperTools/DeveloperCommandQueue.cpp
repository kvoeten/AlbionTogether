#include "DeveloperCommandQueue.h"

#include <utility>

namespace fable::developer_tools
{
    bool DeveloperCommandQueue::Enqueue(DeveloperCommand command) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (size_ == entries_.size())
        {
            return false;
        }

        entries_[writeIndex_] = std::move(command);
        writeIndex_ = (writeIndex_ + 1U) % entries_.size();
        ++size_;
        return true;
    }

    bool DeveloperCommandQueue::TryDequeue(DeveloperCommand& command) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (size_ == 0U)
        {
            return false;
        }

        command = std::move(entries_[readIndex_]);
        readIndex_ = (readIndex_ + 1U) % entries_.size();
        --size_;
        return true;
    }

    std::size_t DeveloperCommandQueue::Size() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_;
    }

    std::size_t DeveloperCommandQueue::Capacity() const noexcept
    {
        return entries_.size();
    }
}
