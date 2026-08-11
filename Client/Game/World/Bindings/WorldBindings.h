#pragma once

#include "Core/Diagnostics/Diagnostics.h"

class asIScriptEngine;

namespace fable::game
{
    class WorldService;
}

namespace fable::core
{
    class CapabilityRegistry;
}

namespace fable::scripting::bindings
{
    bool RegisterWorldBindings(
        asIScriptEngine& engine,
        game::WorldService& worldService,
        core::CapabilityRegistry& capabilities,
        const core::Diagnostics& diagnostics);
}
