#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Scripting/Bindings/Registry/ScriptBindingRegistry.h"

class asIScriptEngine;

namespace fable::game { class GameServiceRuntime; }
namespace fable::scripting { class EventBus; class PersistentStore; class Scheduler; }

namespace fable::scripting::bindings
{
    bool RegisterGameBindings(
        asIScriptEngine& engine,
        game::GameServiceRuntime& services,
        core::CapabilityRegistry& capabilities,
        const core::Diagnostics& diagnostics,
        Scheduler* scheduler = nullptr,
        EventBus* events = nullptr,
        PersistentStore* storage = nullptr);
}
