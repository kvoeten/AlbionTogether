#include "GameBindings.h"

#include <angelscript.h>

namespace fable::scripting::bindings
{
    bool RegisterGameBindings(
        asIScriptEngine& engine,
        game::GameServiceRuntime& services,
        core::CapabilityRegistry& capabilities,
        const core::Diagnostics& diagnostics,
        Scheduler* scheduler,
        EventBus* events,
        PersistentStore* storage)
    {
        return RegisterDiscoveredBindings(
            engine,
            services,
            capabilities,
            diagnostics,
            scheduler,
            events,
            storage);
    }
}
