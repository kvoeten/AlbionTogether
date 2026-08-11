#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

class asIScriptEngine;
class asIScriptFunction;

namespace fable::scripting
{
    class Scheduler final
    {
    public:
        Scheduler() = default;
        ~Scheduler();

        Scheduler(const Scheduler&) = delete;
        Scheduler& operator=(const Scheduler&) = delete;

        void Initialize(asIScriptEngine& engine, const core::Diagnostics& diagnostics);
        std::uint32_t After(float delaySeconds, asIScriptFunction* callback);
        std::uint32_t Every(float intervalSeconds, asIScriptFunction* callback);
        bool Cancel(std::uint32_t taskId);
        void CancelAll();
        void Tick(float deltaSeconds);
        void Shutdown();

        [[nodiscard]] float Time() const noexcept;

    private:
        struct Task
        {
            std::uint32_t id = 0;
            float remainingSeconds = 0.0f;
            float intervalSeconds = 0.0f;
            bool repeating = false;
            asIScriptFunction* callback = nullptr;
        };

        std::uint32_t Schedule(
            float seconds,
            bool repeating,
            asIScriptFunction* callback);
        bool Execute(Task& task);
        void ReleaseTask(Task& task) noexcept;
        void Compact();

        asIScriptEngine* engine_ = nullptr;
        core::Diagnostics diagnostics_ = {};
        std::vector<Task> tasks_;
        std::vector<Task> pendingTasks_;
        std::unordered_set<std::uint32_t> cancelledTasks_;
        std::uint32_t nextTaskId_ = 1;
        float timeSeconds_ = 0.0f;
        bool ticking_ = false;
    };
}
