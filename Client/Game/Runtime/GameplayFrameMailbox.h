#pragma once

#include <array>
#include <cstddef>
#include <mutex>

namespace fable::game
{
    struct GameplayFrameRequests final
    {
        struct Key final { unsigned int code = 0; bool shift = false; };
        std::array<Key, 16> keys{};
        std::size_t keyCount = 0;
        bool worldReady = false;
        bool automationIdle = false;
        bool reload = false;
        bool background = false;
    };

    // Window -> simulation requests, plus one reverse readiness-reset edge.
    // Latest-value flags coalesce; ordered key events have a fixed bound.
    // Native pointers and elapsed-time backlogs never cross this boundary.
    class GameplayFrameMailbox final
    {
    public:
        void WorldReady() { std::lock_guard<std::mutex> lock(mutex_); pending_.worldReady = true; }
        void AutomationIdle() { std::lock_guard<std::mutex> lock(mutex_); pending_.automationIdle = true; }
        void Reload() { std::lock_guard<std::mutex> lock(mutex_); pending_.reload = true; }
        void Background(bool value) { std::lock_guard<std::mutex> lock(mutex_); pending_.background = value; }
        bool KeyPressed(unsigned int code, bool shift)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pending_.keyCount == pending_.keys.size()) return false;
            pending_.keys[pending_.keyCount++] = {code, shift};
            return true;
        }
        GameplayFrameRequests Take()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto result = pending_;
            pending_ = {};
            pending_.background = result.background;
            return result;
        }
        void Departed() { std::lock_guard<std::mutex> lock(mutex_); departed_ = true; }
        bool ConsumeDeparture()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const bool result = departed_;
            departed_ = false;
            return result;
        }
        void Reset()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_ = {};
            departed_ = false;
        }

    private:
        std::mutex mutex_;
        GameplayFrameRequests pending_;
        bool departed_ = false;
    };
}
