#include "QuestBindings.h"
#include "Scripting/Bindings/Registry/ScriptBindingRegistry.h"
#include "Game/Runtime/GameServiceRuntime.h"

#include "Game/Quest/QuestService.h"

#include <angelscript.h>

#include <string>

namespace
{
    fable::game::QuestService* g_quests = nullptr;

    bool IsActive(const std::string& questName) { return g_quests != nullptr && g_quests->IsActive(questName); }
    bool IsRegistered(const std::string& questName) { return g_quests != nullptr && g_quests->IsRegistered(questName); }
    bool IsCompleted(const std::string& questName) { return g_quests != nullptr && g_quests->IsCompleted(questName); }
    bool IsFailed(const std::string& questName) { return g_quests != nullptr && g_quests->IsFailed(questName); }
}

namespace fable::scripting::bindings
{
    bool RegisterQuestBindingGroup(BindingContext& context)
    {
        return RegisterQuestBindings(context.Engine, context.Services.Quests());
    }
}

FABLE_SCRIPT_BINDING_GROUP(Quest, 330, &fable::scripting::bindings::RegisterQuestBindingGroup);

namespace fable::scripting::bindings
{
    bool RegisterQuestBindings(asIScriptEngine& engine, game::QuestService& quests)
    {
        g_quests = &quests;
        int result = engine.SetDefaultNamespace("Quest");
        result = result >= 0 ? engine.RegisterGlobalFunction("bool IsActive(const string &in questName)", asFUNCTION(IsActive), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction("bool IsRegistered(const string &in questName)", asFUNCTION(IsRegistered), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction("bool IsCompleted(const string &in questName)", asFUNCTION(IsCompleted), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction("bool IsFailed(const string &in questName)", asFUNCTION(IsFailed), asCALL_CDECL) : result;
        engine.SetDefaultNamespace("");
        return result >= 0;
    }
}
