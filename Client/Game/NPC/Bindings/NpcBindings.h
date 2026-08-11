#pragma once

class asIScriptEngine;

namespace fable::game
{
    class NpcService;
}

namespace fable::scripting::bindings
{
    bool RegisterNpcBindings(asIScriptEngine& engine, game::NpcService& npcs);
}
