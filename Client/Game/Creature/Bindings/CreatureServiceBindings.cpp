#include "CreatureServiceBindings.h"
#include "Scripting/Bindings/Registry/ScriptBindingRegistry.h"
#include "Game/Runtime/GameServiceRuntime.h"

#include "Game/Creature/CreatureService.h"

#include <angelscript.h>

namespace
{
    fable::game::CreatureService* g_creatures = nullptr;

    bool IsCreature(fable::game::Entity* entity)
    {
        return g_creatures != nullptr && g_creatures->IsCreature(entity);
    }

    float GetHealth(fable::game::Entity* entity)
    {
        return g_creatures != nullptr ? g_creatures->GetHealth(entity) : -1.0f;
    }

    float GetMaximumHealth(fable::game::Entity* entity)
    {
        return g_creatures != nullptr
            ? g_creatures->GetMaximumHealth(entity)
            : -1.0f;
    }

    bool SetHealth(fable::game::Entity* entity, float health)
    {
        return g_creatures != nullptr && g_creatures->SetHealth(entity, health);
    }
}

namespace fable::scripting::bindings
{
    bool RegisterCreatureServiceBindingGroup(BindingContext& context)
    {
        return RegisterCreatureServiceBindings(context.Engine, context.Services.Creatures());
    }
}

FABLE_SCRIPT_BINDING_GROUP(CreatureService, 210, &fable::scripting::bindings::RegisterCreatureServiceBindingGroup);

namespace fable::scripting::bindings
{
    bool RegisterCreatureServiceBindings(
        asIScriptEngine& engine,
        game::CreatureService& creatures)
    {
        g_creatures = &creatures;
        int result = engine.SetDefaultNamespace("Creature");
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool IsCreature(Entity@)", asFUNCTION(IsCreature), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "float GetHealth(Entity@)", asFUNCTION(GetHealth), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "float GetMaximumHealth(Entity@)",
                asFUNCTION(GetMaximumHealth),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool SetHealth(Entity@, float)", asFUNCTION(SetHealth), asCALL_CDECL)
            : result;
        const int resetResult = engine.SetDefaultNamespace("");
        return result >= 0 && resetResult >= 0;
    }
}
