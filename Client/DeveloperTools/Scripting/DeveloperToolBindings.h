#pragma once

class asIScriptEngine;

namespace fable::developer_tools
{
    class DeveloperToolBackend;

    namespace scripting
    {
        void BindDeveloperToolApi(
            DeveloperToolBackend* backend,
            bool hostAuthorized) noexcept;
        bool RegisterDeveloperToolBindings(asIScriptEngine& engine);
    }
}
