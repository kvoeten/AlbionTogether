#pragma once

class asIScriptEngine;

namespace fable::ui
{
    class HudService;
}

namespace fable::scripting::bindings
{
    bool RegisterUiBindings(asIScriptEngine& engine, ui::HudService& hudService);
}
