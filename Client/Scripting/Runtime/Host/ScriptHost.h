#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <Windows.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

class asIScriptEngine;
class asIScriptFunction;

namespace fable::game
{
    class GameServiceRuntime;
}
namespace fable::core { class CapabilityRegistry; }

namespace fable::scripting
{
    class EventBus;
    class PersistentStore;
    class Scheduler;

    class ScriptHost final
    {
    public:
        ScriptHost();
        ~ScriptHost();

        ScriptHost(const ScriptHost&) = delete;
        ScriptHost& operator=(const ScriptHost&) = delete;

        bool Initialize(
            HMODULE clientModule,
            const wchar_t* persistentStorageRoot,
            game::GameServiceRuntime& services,
            const core::Diagnostics& diagnostics);
        void DispatchKeyPressed(unsigned int virtualKey, bool shiftPressed);
        void DispatchWorldReady();
        void DispatchGui();
        void Tick(float deltaSeconds);
        bool Reload();
        void Shutdown();

        [[nodiscard]] bool IsLoaded() const noexcept;

    private:
        struct ScriptModule;

        bool RegisterApi();
        bool LoadModules(const std::filesystem::path& scriptsRoot);
        std::unique_ptr<ScriptModule> BuildModule(
            const std::filesystem::path& scriptPath,
            std::size_t moduleIndex);
        bool Execute(asIScriptFunction* function);
        void ReportExecutionFailure(const char* callbackName, int executionResult);

        asIScriptEngine* engine_ = nullptr;
        std::vector<std::unique_ptr<ScriptModule>> modules_;
        game::GameServiceRuntime* services_ = nullptr;
        std::unique_ptr<core::CapabilityRegistry> capabilities_;
        std::unique_ptr<EventBus> events_;
        std::unique_ptr<PersistentStore> storage_;
        std::unique_ptr<Scheduler> scheduler_;
        std::filesystem::path scriptsRoot_;
        core::Diagnostics diagnostics_ = {};
        mutable std::recursive_mutex executionMutex_;
        bool loaded_ = false;
    };
}
