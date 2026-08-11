#pragma once

class asIScriptEngine;

namespace fable::game::creature::look
{
    class CreatureLookService;
}

namespace fable::scripting::bindings
{
    bool RegisterCreatureLookBindings(
        asIScriptEngine& engine,
        game::creature::look::CreatureLookService& service);
}
