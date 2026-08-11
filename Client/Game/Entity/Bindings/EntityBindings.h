#pragma once

class asIScriptEngine;

namespace fable::scripting::bindings
{
    bool RegisterEntityTypes(asIScriptEngine& engine);
    bool RegisterEntityMembers(asIScriptEngine& engine);
}
