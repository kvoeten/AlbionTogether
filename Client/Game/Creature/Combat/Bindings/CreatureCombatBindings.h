#pragma once

class asIScriptEngine;

namespace fable::game::creature::combat
{
    class CreatureCombatService;
}

namespace fable::scripting::bindings
{
    bool RegisterCreatureCombatBindings(
        asIScriptEngine& engine,
        game::creature::combat::CreatureCombatService& service);
}
