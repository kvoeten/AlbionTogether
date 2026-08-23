#include "PlayerBindings.h"
#include "Scripting/Bindings/Registry/ScriptBindingRegistry.h"
#include "Game/Runtime/GameServiceRuntime.h"

#include "Game/Player/PlayerService.h"

#include <angelscript.h>

namespace
{
    fable::game::PlayerService* g_player = nullptr;

    fable::game::Entity* GetHero()
    {
        return g_player != nullptr ? g_player->GetHero() : nullptr;
    }

    float GetHealth()
    {
        return g_player != nullptr ? g_player->GetHealth() : -1.0f;
    }

    float GetMaximumHealth()
    {
        return g_player != nullptr ? g_player->GetMaximumHealth() : -1.0f;
    }

    bool SetHealth(float health)
    {
        return g_player != nullptr && g_player->SetHealth(health);
    }
}

namespace fable::scripting::bindings
{
    bool RegisterPlayerBindingGroup(BindingContext& context)
    {
        return RegisterPlayerBindings(context.Engine, context.Services.Players());
    }
}

FABLE_SCRIPT_BINDING_GROUP(Player, 300, &fable::scripting::bindings::RegisterPlayerBindingGroup);

namespace fable::scripting::bindings
{
    bool RegisterPlayerBindings(
        asIScriptEngine& engine,
        game::PlayerService& player)
    {
        g_player = &player;
        int result = engine.SetDefaultNamespace("Player");
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "Entity@+ GetHero()", asFUNCTION(GetHero), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "float GetHealth()", asFUNCTION(GetHealth), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "float GetMaximumHealth()", asFUNCTION(GetMaximumHealth), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool SetHealth(float)", asFUNCTION(SetHealth), asCALL_CDECL)
            : result;
        const int resetResult = engine.SetDefaultNamespace("");
        return result >= 0 && resetResult >= 0;
    }
}
