#include "CreatureCombatBindings.h"

#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/Entity/Entity.h"

#include <angelscript.h>

namespace
{
    fable::game::creature::combat::CreatureCombatService* g_combat = nullptr;

    bool RoutePlayerCombat(fable::game::Entity* hero, fable::game::Entity* puppet)
    {
        return g_combat != nullptr && g_combat->RoutePlayerCombat(hero, puppet);
    }

    void ClearPlayerCombat()
    {
        if (g_combat != nullptr)
        {
            g_combat->ClearPlayerCombat();
        }
    }

    bool PlayerCombatRouterActive()
    {
        return g_combat != nullptr && g_combat->IsPlayerCombatRouted();
    }

    unsigned int RoutedPlayerAttackCount()
    {
        return g_combat != nullptr ? g_combat->RoutedPlayerAttackCount() : 0;
    }
}

namespace fable::scripting::bindings
{
    bool RegisterCreatureCombatBindings(
        asIScriptEngine& engine,
        game::creature::combat::CreatureCombatService& service)
    {
        g_combat = &service;
        return engine.SetDefaultNamespace("Creature") >= 0 &&
            engine.RegisterGlobalFunction(
                "bool RoutePlayerCombat(Entity@, Entity@)",
                asFUNCTION(RoutePlayerCombat),
                asCALL_CDECL) >= 0 &&
            engine.RegisterGlobalFunction(
                "void ClearPlayerCombat()",
                asFUNCTION(ClearPlayerCombat),
                asCALL_CDECL) >= 0 &&
            engine.RegisterGlobalFunction(
                "bool PlayerCombatRouterActive()",
                asFUNCTION(PlayerCombatRouterActive),
                asCALL_CDECL) >= 0 &&
            engine.RegisterGlobalFunction(
                "uint RoutedPlayerAttackCount()",
                asFUNCTION(RoutedPlayerAttackCount),
                asCALL_CDECL) >= 0 &&
            engine.SetDefaultNamespace("") >= 0;
    }
}
