#include "Scheduler.h"

#include <angelscript.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fable::scripting
{
    Scheduler::~Scheduler()
    {
        Shutdown();
    }

    void Scheduler::Initialize(
        asIScriptEngine& engine,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        engine_ = &engine;
        diagnostics_ = diagnostics;
    }

    std::uint32_t Scheduler::After(float delaySeconds, asIScriptFunction* callback)
    {
        return Schedule(delaySeconds, false, callback);
    }

    std::uint32_t Scheduler::Every(float intervalSeconds, asIScriptFunction* callback)
    {
        return Schedule(intervalSeconds, true, callback);
    }

    std::uint32_t Scheduler::Schedule(
        float seconds,
        bool repeating,
        asIScriptFunction* callback)
    {
        if (engine_ == nullptr || callback == nullptr ||
            !std::isfinite(seconds) || seconds < 0.0f)
        {
            return 0;
        }

        Task task;
        task.id = nextTaskId_++;
        if (task.id == 0)
        {
            task.id = nextTaskId_++;
        }
        task.remainingSeconds = repeating ? std::max(seconds, 0.001f) : seconds;
        task.intervalSeconds = task.remainingSeconds;
        task.repeating = repeating;
        task.callback = callback;
        task.callback->AddRef();
        (ticking_ ? pendingTasks_ : tasks_).push_back(task);
        return task.id;
    }

    bool Scheduler::Cancel(std::uint32_t taskId)
    {
        if (taskId == 0)
        {
            return false;
        }
        const auto exists = [taskId](const Task& task) { return task.id == taskId; };
        const bool found =
            std::any_of(tasks_.begin(), tasks_.end(), exists) ||
            std::any_of(pendingTasks_.begin(), pendingTasks_.end(), exists);
        if (!found)
        {
            return false;
        }
        cancelledTasks_.insert(taskId);
        if (!ticking_)
        {
            Compact();
        }
        return true;
    }

    void Scheduler::CancelAll()
    {
        for (const Task& task : tasks_)
        {
            cancelledTasks_.insert(task.id);
        }
        for (const Task& task : pendingTasks_)
        {
            cancelledTasks_.insert(task.id);
        }
        if (!ticking_)
        {
            Compact();
        }
    }

    void Scheduler::Tick(float deltaSeconds)
    {
        if (engine_ == nullptr || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0f)
        {
            return;
        }
        timeSeconds_ += deltaSeconds;
        ticking_ = true;
        for (Task& task : tasks_)
        {
            if (cancelledTasks_.count(task.id) != 0)
            {
                continue;
            }
            task.remainingSeconds -= deltaSeconds;
            if (task.remainingSeconds > 0.0f)
            {
                continue;
            }

            const bool completed = Execute(task);
            if (!task.repeating || !completed)
            {
                cancelledTasks_.insert(task.id);
                continue;
            }
            task.remainingSeconds += task.intervalSeconds;
            if (task.remainingSeconds <= 0.0f)
            {
                task.remainingSeconds = task.intervalSeconds;
            }
        }
        ticking_ = false;
        Compact();
    }

    bool Scheduler::Execute(Task& task)
    {
        asIScriptContext* context = engine_->CreateContext();
        if (context == nullptr)
        {
            diagnostics_.Log("AngelScript scheduler: execution context allocation failed.");
            return false;
        }
        const int prepareResult = context->Prepare(task.callback);
        const int executionResult = prepareResult >= 0
            ? context->Execute()
            : prepareResult;
        if (executionResult != asEXECUTION_FINISHED)
        {
            char detail[384] = {};
            const char* exception = context->GetExceptionString();
            std::snprintf(
                detail,
                sizeof(detail),
                "task=%lu callback=%s result=%d exception=%s",
                static_cast<unsigned long>(task.id),
                task.callback->GetDeclaration(),
                executionResult,
                exception != nullptr ? exception : "<none>");
            diagnostics_.Event("ScriptScheduledCallbackFailed", detail);
        }
        context->Release();
        return executionResult == asEXECUTION_FINISHED;
    }

    void Scheduler::ReleaseTask(Task& task) noexcept
    {
        if (task.callback != nullptr)
        {
            task.callback->Release();
            task.callback = nullptr;
        }
    }

    void Scheduler::Compact()
    {
        const auto removeCancelled = [this](Task& task)
        {
            if (cancelledTasks_.count(task.id) == 0)
            {
                return false;
            }
            ReleaseTask(task);
            return true;
        };
        tasks_.erase(
            std::remove_if(tasks_.begin(), tasks_.end(), removeCancelled),
            tasks_.end());
        pendingTasks_.erase(
            std::remove_if(pendingTasks_.begin(), pendingTasks_.end(), removeCancelled),
            pendingTasks_.end());
        tasks_.insert(tasks_.end(), pendingTasks_.begin(), pendingTasks_.end());
        pendingTasks_.clear();
        cancelledTasks_.clear();
    }

    void Scheduler::Shutdown()
    {
        ticking_ = false;
        for (Task& task : tasks_)
        {
            ReleaseTask(task);
        }
        for (Task& task : pendingTasks_)
        {
            ReleaseTask(task);
        }
        tasks_.clear();
        pendingTasks_.clear();
        cancelledTasks_.clear();
        engine_ = nullptr;
        diagnostics_ = {};
        nextTaskId_ = 1;
        timeSeconds_ = 0.0f;
    }

    float Scheduler::Time() const noexcept
    {
        return timeSeconds_;
    }
}
