#pragma once

class asIScriptEngine;

namespace fable::game
{
    class HeroPawnService;
}

namespace fable::scripting::bindings
{
    bool RegisterHeroPawnBindings(
        asIScriptEngine& engine,
        game::HeroPawnService& heroPawn);
}
