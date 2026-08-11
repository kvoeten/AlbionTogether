#pragma once

class asIScriptEngine;

namespace fable::scripting
{
    class PersistentStore;
}

namespace fable::scripting::bindings
{
    bool RegisterStorageBindings(asIScriptEngine& engine, PersistentStore& storage);
}
