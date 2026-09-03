#pragma once

class asIScriptEngine;

namespace fable::ui::imgui::bindings
{
    bool RegisterImGuiBindings(asIScriptEngine& engine);

    // ImGui functions are valid only while the DX9 bridge is dispatching an
    // AngelScript OnGui callback on the render thread.
    void BeginScriptFrame() noexcept;
    void EndScriptFrame() noexcept;
}
