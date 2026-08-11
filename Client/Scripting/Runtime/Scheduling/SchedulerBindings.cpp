#include "SchedulerBindings.h"

#include "Scheduler.h"

#include <angelscript.h>

namespace
{
    fable::scripting::Scheduler* g_scheduler = nullptr;

    asUINT After(float delaySeconds, asIScriptFunction* callback)
    {
        return g_scheduler != nullptr
            ? g_scheduler->After(delaySeconds, callback)
            : 0;
    }

    asUINT Every(float intervalSeconds, asIScriptFunction* callback)
    {
        return g_scheduler != nullptr
            ? g_scheduler->Every(intervalSeconds, callback)
            : 0;
    }

    bool Cancel(asUINT taskId)
    {
        return g_scheduler != nullptr && g_scheduler->Cancel(taskId);
    }

    void CancelAll()
    {
        if (g_scheduler != nullptr)
        {
            g_scheduler->CancelAll();
        }
    }

    float GetTime()
    {
        return g_scheduler != nullptr ? g_scheduler->Time() : 0.0f;
    }
}

namespace fable::scripting::bindings
{
    bool RegisterSchedulerBindings(asIScriptEngine& engine, Scheduler& scheduler)
    {
        g_scheduler = &scheduler;
        int result = engine.RegisterFuncdef("void ScheduledCallback()");
        result = result >= 0 ? engine.SetDefaultNamespace("Scheduler") : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "uint After(float delaySeconds, ScheduledCallback@ callback)",
                asFUNCTION(After),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "uint Every(float intervalSeconds, ScheduledCallback@ callback)",
                asFUNCTION(Every),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction("bool Cancel(uint taskId)", asFUNCTION(Cancel), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction("void CancelAll()", asFUNCTION(CancelAll), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction("float get_Time()", asFUNCTION(GetTime), asCALL_CDECL)
            : result;
        engine.SetDefaultNamespace("");
        return result >= 0;
    }
}
