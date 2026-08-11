#include "EventBindings.h"

#include "EventBus.h"

#include <angelscript.h>

#include <string>

namespace
{
    fable::scripting::EventBus* g_events = nullptr;

    asUINT Subscribe(const std::string& eventName, asIScriptFunction* callback)
    {
        return g_events != nullptr ? g_events->Subscribe(eventName, callback) : 0;
    }

    bool Unsubscribe(asUINT subscriptionId)
    {
        return g_events != nullptr && g_events->Unsubscribe(subscriptionId);
    }

    asUINT Emit(const std::string& eventName, const std::string& detail)
    {
        return g_events != nullptr ? g_events->Emit(eventName, detail) : 0;
    }
}

namespace fable::scripting::bindings
{
    bool RegisterEventBindings(asIScriptEngine& engine, EventBus& events)
    {
        g_events = &events;
        int result = engine.RegisterFuncdef(
            "void EventCallback(const string &in eventName, const string &in detail)");
        result = result >= 0 ? engine.SetDefaultNamespace("Events") : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "uint Subscribe(const string &in eventName, EventCallback@ callback)",
                asFUNCTION(Subscribe),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool Unsubscribe(uint subscriptionId)",
                asFUNCTION(Unsubscribe),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "uint Emit(const string &in eventName, const string &in detail = '')",
                asFUNCTION(Emit),
                asCALL_CDECL)
            : result;
        engine.SetDefaultNamespace("");
        return result >= 0;
    }
}
