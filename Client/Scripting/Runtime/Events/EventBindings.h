#pragma once

class asIScriptEngine;

namespace fable::scripting
{
    class EventBus;
}

namespace fable::scripting::bindings
{
    bool RegisterEventBindings(asIScriptEngine& engine, EventBus& events);
}
