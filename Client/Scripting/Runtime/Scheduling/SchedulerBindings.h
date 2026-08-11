#pragma once

class asIScriptEngine;

namespace fable::scripting
{
    class Scheduler;
}

namespace fable::scripting::bindings
{
    bool RegisterSchedulerBindings(asIScriptEngine& engine, Scheduler& scheduler);
}
