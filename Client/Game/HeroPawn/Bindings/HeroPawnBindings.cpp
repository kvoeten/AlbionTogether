#include "HeroPawnBindings.h"

#include "Game/HeroPawn/HeroPawnService.h"

#include <angelscript.h>

namespace
{
    fable::game::HeroPawnService* g_heroPawn = nullptr;

    fable::game::Entity* Get()
    {
        return g_heroPawn != nullptr ? g_heroPawn->Get() : nullptr;
    }

    bool SetVisible(fable::game::Entity* hero, bool visible)
    {
        return g_heroPawn != nullptr && g_heroPawn->SetVisible(hero, visible);
    }
}

namespace fable::scripting::bindings
{
    bool RegisterHeroPawnBindings(
        asIScriptEngine& engine,
        game::HeroPawnService& heroPawn)
    {
        g_heroPawn = &heroPawn;
        int result = engine.SetDefaultNamespace("HeroPawn");
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "Entity@+ Get()", asFUNCTION(Get), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool SetVisible(Entity@, bool)", asFUNCTION(SetVisible), asCALL_CDECL)
            : result;
        const int resetResult = engine.SetDefaultNamespace("");
        return result >= 0 && resetResult >= 0;
    }
}
