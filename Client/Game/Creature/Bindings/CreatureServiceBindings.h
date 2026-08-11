#pragma once

class asIScriptEngine;

namespace fable::game
{
    class CreatureService;
}

namespace fable::scripting::bindings
{
    bool RegisterCreatureServiceBindings(
        asIScriptEngine& engine,
        game::CreatureService& creatures);
}
