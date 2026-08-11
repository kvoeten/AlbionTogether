#include "CreatureLookBindings.h"

#include "Game/Creature/Look/CreatureLookService.h"
#include "Game/Entity/Entity.h"

#include <angelscript.h>

namespace
{
    fable::game::creature::look::CreatureLookService* g_look = nullptr;

    bool RouteMovementFacing(fable::game::Entity* target)
    {
        return g_look != nullptr && g_look->RouteMovementFacing(target);
    }

    void ClearMovementFacing()
    {
        if (g_look != nullptr)
        {
            g_look->ClearMovementFacing();
        }
    }

    unsigned int RoutedMovementFacingCount()
    {
        return g_look != nullptr ? g_look->RoutedMovementFacingCount() : 0;
    }
}

namespace fable::scripting::bindings
{
    bool RegisterCreatureLookBindings(
        asIScriptEngine& engine,
        game::creature::look::CreatureLookService& service)
    {
        g_look = &service;
        return engine.SetDefaultNamespace("Creature") >= 0 &&
            engine.RegisterGlobalFunction(
                "bool RouteMovementFacing(Entity@)",
                asFUNCTION(RouteMovementFacing),
                asCALL_CDECL) >= 0 &&
            engine.RegisterGlobalFunction(
                "void ClearMovementFacing()",
                asFUNCTION(ClearMovementFacing),
                asCALL_CDECL) >= 0 &&
            engine.RegisterGlobalFunction(
                "uint RoutedMovementFacingCount()",
                asFUNCTION(RoutedMovementFacingCount),
                asCALL_CDECL) >= 0 &&
            engine.SetDefaultNamespace("") >= 0;
    }
}
