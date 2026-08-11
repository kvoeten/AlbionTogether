#pragma once

class asIScriptEngine;

namespace fable::game
{
    class PlayerService;
}

namespace fable::scripting::bindings
{
    bool RegisterPlayerBindings(
        asIScriptEngine& engine,
        game::PlayerService& player);
}
