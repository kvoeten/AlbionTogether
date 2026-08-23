#include "NpcBindings.h"
#include "Scripting/Bindings/Registry/ScriptBindingRegistry.h"
#include "Game/Runtime/GameServiceRuntime.h"

#include "Game/NPC/NpcService.h"

#include <angelscript.h>

namespace
{
    fable::game::NpcService* g_npcs = nullptr;

    fable::game::Entity* Spawn(
        const std::string& definition,
        const fable::game::Vector3& position,
        const std::string& scriptName)
    {
        return g_npcs != nullptr
            ? g_npcs->Spawn(definition, position, scriptName)
            : nullptr;
    }

    fable::game::ScriptControl* TakeControl(
        fable::game::Entity* npc,
        fable::game::AiPriority priority)
    {
        return g_npcs != nullptr ? g_npcs->TakeControl(npc, priority) : nullptr;
    }
}

namespace fable::scripting::bindings
{
    bool RegisterNpcBindingGroup(BindingContext& context)
    {
        return RegisterNpcBindings(context.Engine, context.Services.Npcs());
    }
}

FABLE_SCRIPT_BINDING_GROUP(Npc, 310, &fable::scripting::bindings::RegisterNpcBindingGroup);

namespace fable::scripting::bindings
{
    bool RegisterNpcBindings(asIScriptEngine& engine, game::NpcService& npcs)
    {
        g_npcs = &npcs;
        int result = engine.SetDefaultNamespace("NPC");
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "Entity@+ Spawn(const string &in, const Vector3 &in, const string &in scriptName = '')",
                asFUNCTION(Spawn),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "CreatureControl@+ TakeControl(Entity@, AiPriority priority = Highest)",
                asFUNCTION(TakeControl),
                asCALL_CDECL)
            : result;
        const int resetResult = engine.SetDefaultNamespace("");
        return result >= 0 && resetResult >= 0;
    }
}
