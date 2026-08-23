#include "UiBindings.h"
#include "Scripting/Bindings/Registry/ScriptBindingRegistry.h"
#include "Game/Runtime/GameServiceRuntime.h"

#include "UI/Hud/HudService.h"

#include <angelscript.h>

namespace
{
    fable::ui::HudService* g_hudService = nullptr;

    bool ShowMessage(const std::string& textGroup, int selectionMethod)
    {
        return g_hudService != nullptr &&
            g_hudService->ShowMessage(textGroup, selectionMethod);
    }
}

namespace fable::scripting::bindings
{
    bool RegisterUiBindingGroup(BindingContext& context)
    {
        return RegisterUiBindings(context.Engine, context.Services.Hud());
    }
}

FABLE_SCRIPT_BINDING_GROUP(Ui, 350, &fable::scripting::bindings::RegisterUiBindingGroup);

namespace fable::scripting::bindings
{
    bool RegisterUiBindings(asIScriptEngine& engine, ui::HudService& hudService)
    {
        g_hudService = &hudService;
        int result = engine.SetDefaultNamespace("UI");
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool ShowMessage(const string &in textGroup, int selectionMethod = 2)",
                asFUNCTION(ShowMessage),
                asCALL_CDECL)
            : result;
        const int resetResult = engine.SetDefaultNamespace("");
        return result >= 0 && resetResult >= 0;
    }
}
