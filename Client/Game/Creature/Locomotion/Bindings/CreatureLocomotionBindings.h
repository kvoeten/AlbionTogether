#pragma once

class asIScriptEngine;

namespace fable::game::creature::locomotion
{
    class CreatureLocomotionService;
}

namespace fable::scripting::bindings
{
    bool RegisterCreatureLocomotionBindings(
        asIScriptEngine& engine,
        game::creature::locomotion::CreatureLocomotionService& locomotion);
}
