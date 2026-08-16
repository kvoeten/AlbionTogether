#include "ScriptHost.h"

#include "Automation/LocalInstance/MapTransitionAcceptanceDriver.h"
#include "Automation/Multiplayer/Combat/CombatTargetAcceptanceDriver.h"
#include "Automation/Multiplayer/Transition/NpcTransferAcceptanceDriver.h"
#include "Automation/Runtime/RuntimeConfiguration.h"
#include "Core/Capabilities/CapabilityRegistry.h"
#include "Game/Creature/CreatureService.h"
#include "Game/Creature/Animation/CreatureAnimationService.h"
#include "Game/Creature/Combat/CreatureCombatService.h"
#include "Game/Creature/Look/CreatureLookService.h"
#include "Game/Creature/Locomotion/CreatureLocomotionService.h"
#include "Game/Entity/EntityService.h"
#include "Game/Entity/Presence/Hooks/ThingPresenceObserver.h"
#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/HeroPawn/HeroPawnService.h"
#include "Game/NPC/NpcService.h"
#include "Game/NPC/Simulation/DummyVillager/DummyVillagerService.h"
#include "Game/NPC/Village/VillageMembershipService.h"
#include "Game/Player/PlayerService.h"
#include "Game/Player/Input/PlayerInputService.h"
#include "Game/Quest/QuestService.h"
#include "Game/World/WorldService.h"
#include "Scripting/Bindings/Registry/GameBindings.h"
#include "Scripting/Runtime/Capabilities/ApiCoverage.h"
#include "Scripting/Runtime/Events/EventBindings.h"
#include "Scripting/Runtime/Events/EventBus.h"
#include "Scripting/Runtime/Scheduling/Scheduler.h"
#include "Scripting/Runtime/Scheduling/SchedulerBindings.h"
#include "Scripting/Runtime/Storage/PersistentStore.h"
#include "Scripting/Runtime/Storage/StorageBindings.h"
#include "Multiplayer/Runtime/MultiplayerSession.h"
#include "UI/Hud/HudService.h"

#include <angelscript.h>
#include <scriptarray.h>
#include <scriptstdstring.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    fable::core::Diagnostics g_diagnostics;

    void MessageCallback(const asSMessageInfo* message, void*)
    {
        const char* severity = "info";
        if (message->type == asMSGTYPE_ERROR)
        {
            severity = "error";
        }
        else if (message->type == asMSGTYPE_WARNING)
        {
            severity = "warning";
        }

        char formatted[1024] = {};
        std::snprintf(
            formatted,
            sizeof(formatted),
            "AngelScript %s: %s(%d,%d): %s",
            severity,
            message->section != nullptr ? message->section : "<unknown>",
            message->row,
            message->col,
            message->message != nullptr ? message->message : "<no message>");
        g_diagnostics.Log(formatted);
    }

    bool ResolveScriptsRoot(HMODULE module, std::filesystem::path& scriptsRoot)
    {
        wchar_t path[32'768] = {};
        const DWORD length = GetModuleFileNameW(
            module,
            path,
            static_cast<DWORD>(std::size(path)));
        if (length == 0 || length >= std::size(path))
        {
            return false;
        }

        scriptsRoot = std::filesystem::path(path).parent_path() / L"scripts";
        return true;
    }

    std::string PathToUtf8(const std::filesystem::path& path)
    {
        const std::wstring value = path.wstring();
        const int required = WideCharToMultiByte(
            CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1)
        {
            return {};
        }
        std::string result(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            value.c_str(),
            -1,
            result.data(),
            required,
            nullptr,
            nullptr);
        result.pop_back();
        return result;
    }
}

namespace fable::scripting
{
    struct ScriptHost::ScriptModule
    {
        std::string name;
        std::string internalName;
        std::filesystem::path path;
        asIScriptFunction* onLoad = nullptr;
        asIScriptFunction* onUnload = nullptr;
        asIScriptFunction* onKeyPressed = nullptr;
        asIScriptFunction* onWorldReady = nullptr;
        asIScriptFunction* onTick = nullptr;
        bool enabled = true;
    };

    ScriptHost::ScriptHost()
        : entityService_(std::make_unique<game::EntityService>()),
          creatureService_(std::make_unique<game::CreatureService>()),
          creatureLocomotionService_(
              std::make_unique<game::creature::locomotion::CreatureLocomotionService>()),
          creatureLookService_(
              std::make_unique<game::creature::look::CreatureLookService>()),
          creatureCombatService_(
              std::make_unique<game::creature::combat::CreatureCombatService>()),
          creatureAnimationService_(
              std::make_unique<game::creature::animation::CreatureAnimationService>()),
          playerService_(std::make_unique<game::PlayerService>()),
          playerInputService_(
              std::make_unique<game::player::input::PlayerInputService>()),
          questService_(std::make_unique<game::QuestService>()),
          npcService_(std::make_unique<game::NpcService>()),
          villageMembershipService_(std::make_unique<
              game::npc::village::VillageMembershipService>()),
          dummyVillagerService_(std::make_unique<
              game::npc::simulation::DummyVillagerService>()),
          heroPawnService_(std::make_unique<game::HeroPawnService>()),
          worldService_(std::make_unique<game::WorldService>()),
          hudService_(std::make_unique<ui::HudService>()),
          capabilities_(std::make_unique<core::CapabilityRegistry>()),
          events_(std::make_unique<EventBus>()),
          storage_(std::make_unique<PersistentStore>()),
          scheduler_(std::make_unique<Scheduler>()),
          transitionAcceptanceDriver_(std::make_unique<
              automation::local_instance::MapTransitionAcceptanceDriver>()),
          combatTargetAcceptanceDriver_(std::make_unique<
              automation::multiplayer::combat::
                  CombatTargetAcceptanceDriver>()),
          npcTransferAcceptanceDriver_(std::make_unique<
              automation::multiplayer::transition::
                  NpcTransferAcceptanceDriver>()),
          multiplayerSession_(std::make_unique<multiplayer::MultiplayerSession>())
    {
    }

    ScriptHost::~ScriptHost()
    {
        Shutdown();
    }

    bool ScriptHost::Initialize(
        HMODULE clientModule,
        HMODULE gameModule,
        const wchar_t* persistentStorageRoot,
        const automation::runtime::RuntimeConfiguration& runtimeConfiguration,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        diagnostics_ = diagnostics;
        g_diagnostics = diagnostics;

        if (!entityService_->Initialize(gameModule, diagnostics_))
        {
            diagnostics_.Log("AngelScript: native Entity service initialization failed.");
            return false;
        }
        if (!hudService_->Initialize(entityService_->Interface(), diagnostics_))
        {
            diagnostics_.Log("AngelScript: native HUD service initialization failed.");
            return false;
        }
        if (!creatureService_->Initialize(*entityService_, diagnostics_) ||
            !creatureLocomotionService_->Initialize(*entityService_, diagnostics_) ||
            !creatureLookService_->Initialize(*entityService_, diagnostics_) ||
            !creatureCombatService_->Initialize(*entityService_, diagnostics_) ||
            !creatureAnimationService_->Initialize(*entityService_, diagnostics_) ||
            !villageMembershipService_->Initialize(gameModule, diagnostics_) ||
            !dummyVillagerService_->Initialize(gameModule, diagnostics_) ||
            !playerInputService_->Initialize(gameModule, diagnostics_) ||
            !playerService_->Initialize(
                *entityService_,
                *creatureService_,
                diagnostics_) ||
            !npcService_->Initialize(*entityService_, diagnostics_) ||
            !heroPawnService_->Initialize(*entityService_, diagnostics_) ||
            !questService_->Initialize(*entityService_, diagnostics_) ||
            !worldService_->Initialize(*entityService_, diagnostics_))
        {
            diagnostics_.Log("AngelScript: creature or player service initialization failed.");
            return false;
        }

        if (!multiplayerSession_->Initialize(
                runtimeConfiguration,
                *entityService_,
                *npcService_,
                *creatureLocomotionService_,
                *creatureLookService_,
                *creatureCombatService_,
                *creatureAnimationService_,
                *villageMembershipService_,
                *dummyVillagerService_,
                diagnostics_))
        {
            diagnostics_.Log("Multiplayer: session initialization failed.");
            return false;
        }
        transitionAcceptanceDriver_->Initialize(
            runtimeConfiguration.ScenarioIs(L"multiplayer_host_transition") ||
                runtimeConfiguration.ScenarioIs(
                    L"multiplayer_host_authority") ||
                runtimeConfiguration.ScenarioIs(
                    L"multiplayer_guest_transition"),
            runtimeConfiguration.ScenarioIs(
                L"multiplayer_host_authority"),
            *entityService_,
            *creatureLocomotionService_,
            diagnostics_);
        combatTargetAcceptanceDriver_->Initialize(
            runtimeConfiguration.ScenarioIs(L"multiplayer_host_combat") ||
                runtimeConfiguration.ScenarioIs(
                    L"multiplayer_guest_combat"),
            runtimeConfiguration.ScenarioIs(L"multiplayer_host_combat"),
            *entityService_,
            *creatureService_,
            *npcService_,
            diagnostics_);
        npcTransferAcceptanceDriver_->Initialize(
            runtimeConfiguration.ScenarioIs(L"multiplayer_host_transition"),
            *entityService_,
            *npcService_,
            *multiplayerSession_,
            diagnostics_);
        RegisterApiCoverage(*capabilities_);

        std::filesystem::path scriptsRoot;
        if (!ResolveScriptsRoot(clientModule, scriptsRoot))
        {
            diagnostics_.Log("AngelScript: could not resolve the deployed scripts directory.");
            return false;
        }
        scriptsRoot_ = scriptsRoot;

        engine_ = asCreateScriptEngine();
        if (engine_ == nullptr)
        {
            diagnostics_.Log("AngelScript: engine creation failed.");
            return false;
        }
        events_->Initialize(*engine_, diagnostics_);
        const std::filesystem::path storageRoot =
            persistentStorageRoot != nullptr && persistentStorageRoot[0] != L'\0'
                ? std::filesystem::path(persistentStorageRoot)
                : scriptsRoot_.parent_path() / L"script-data";
        storage_->Initialize(*engine_, storageRoot, diagnostics_);
        diagnostics_.Event(
            "ScriptStorageRootReady",
            PathToUtf8(storageRoot).c_str());
        scheduler_->Initialize(*engine_, diagnostics_);

        // Expose native get_/set_ methods through idiomatic script properties.
        // AngelScript 2.38 defaults to requiring an explicit `property` marker;
        // mode 1 preserves the established application-registered accessor ABI.
        if (engine_->SetEngineProperty(asEP_PROPERTY_ACCESSOR_MODE, 1) < 0)
        {
            diagnostics_.Log("AngelScript: property accessor configuration failed.");
            Shutdown();
            return false;
        }

        int result = engine_->SetMessageCallback(
            asFUNCTION(MessageCallback),
            nullptr,
            asCALL_CDECL);
        if (result < 0)
        {
            diagnostics_.Log("AngelScript: message callback registration failed.");
            Shutdown();
            return false;
        }

        RegisterStdString(engine_);
        RegisterScriptArray(engine_, true);
        if (!RegisterApi())
        {
            Shutdown();
            return false;
        }

        if (!LoadModules(scriptsRoot_))
        {
            Shutdown();
            return false;
        }

        loaded_ = true;
        for (const auto& module : modules_)
        {
            if (module->onLoad != nullptr && !Execute(module->onLoad))
            {
                module->enabled = false;
                diagnostics_.Event("ScriptModuleDisabled", module->name.c_str());
            }
        }
        const bool anyEnabled = std::any_of(
            modules_.begin(),
            modules_.end(),
            [](const std::unique_ptr<ScriptModule>& module) { return module->enabled; });
        if (!anyEnabled)
        {
            diagnostics_.Log("AngelScript: no module completed its load callback.");
            Shutdown();
            return false;
        }

        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "AngelScript runtime loaded %zu module(s) with the gameplay service API",
            modules_.size());
        diagnostics_.Event("ScriptRuntimeReady", detail);
        return true;
    }

    bool ScriptHost::AttachThingPresenceObserver(
        game::entity::presence::ThingPresenceObserver& observer)
    {
        return multiplayerSession_->AttachThingPresenceObserver(observer);
    }

    bool ScriptHost::AttachSavedEntityMapBlobObserver(
        game::entity::persistence::SavedEntityMapBlobObserver& observer)
    {
        return multiplayerSession_->AttachSavedEntityMapBlobObserver(observer);
    }

    bool ScriptHost::AttachThingSaveProjectionHook(
        game::entity::persistence::ThingSaveProjectionHook& hook)
    {
        return multiplayerSession_->AttachThingSaveProjectionHook(hook);
    }

    bool ScriptHost::AttachPopulationSimulationHook(
        game::npc::population::PopulationSimulationHook& hook)
    {
        return multiplayerSession_->AttachPopulationSimulationHook(hook);
    }

    bool ScriptHost::AttachCreatureActionObserver(
        game::creature::actions::CreatureActionLifecycleObserver& observer)
    {
        return multiplayerSession_->AttachCreatureActionObserver(observer);
    }

    bool ScriptHost::AttachAiBrainUpdateObserver(
        game::creature::ai::AiBrainUpdateObserver& observer)
    {
        return multiplayerSession_->AttachAiBrainUpdateObserver(observer);
    }

    bool ScriptHost::AttachWorldTravelObserver(
        game::world::travel::WorldTravelObserver& observer)
    {
        return multiplayerSession_->AttachWorldTravelObserver(observer);
    }

    bool ScriptHost::RegisterApi()
    {
        if (!bindings::RegisterGameBindings(
                *engine_,
                *creatureService_,
                *creatureLocomotionService_,
                *creatureLookService_,
                *creatureCombatService_,
                *playerService_,
                *npcService_,
                *heroPawnService_,
                *questService_,
                *worldService_,
                *hudService_,
                *capabilities_,
                diagnostics_))
        {
            diagnostics_.Log("AngelScript: game API registration failed.");
            return false;
        }
        if (!bindings::RegisterSchedulerBindings(*engine_, *scheduler_))
        {
            diagnostics_.Log("AngelScript: scheduler API registration failed.");
            return false;
        }
        if (!bindings::RegisterEventBindings(*engine_, *events_))
        {
            diagnostics_.Log("AngelScript: event API registration failed.");
            return false;
        }
        if (!bindings::RegisterStorageBindings(*engine_, *storage_))
        {
            diagnostics_.Log("AngelScript: storage API registration failed.");
            return false;
        }
        return true;
    }

    bool ScriptHost::LoadModules(const std::filesystem::path& scriptsRoot)
    {
        std::error_code error;
        if (!std::filesystem::exists(scriptsRoot, error) || error)
        {
            const std::string path = PathToUtf8(scriptsRoot);
            std::string message = "AngelScript: scripts directory is unavailable: " + path;
            diagnostics_.Log(message.c_str());
            return false;
        }

        std::vector<std::filesystem::path> scripts;
        for (std::filesystem::recursive_directory_iterator iterator(scriptsRoot, error), end;
             iterator != end && !error;
             iterator.increment(error))
        {
            if (iterator->is_regular_file(error) &&
                !error &&
                iterator->path().extension() == L".as")
            {
                scripts.push_back(iterator->path());
            }
        }
        if (error)
        {
            diagnostics_.Log("AngelScript: scripts directory enumeration failed.");
            return false;
        }
        std::sort(scripts.begin(), scripts.end());
        if (scripts.empty())
        {
            diagnostics_.Log("AngelScript: no .as modules were found in the deployed scripts directory.");
            return false;
        }

        for (std::size_t index = 0; index < scripts.size(); ++index)
        {
            std::unique_ptr<ScriptModule> module = BuildModule(scripts[index], index);
            if (module == nullptr)
            {
                const std::string path = PathToUtf8(scripts[index]);
                diagnostics_.Event("ScriptModuleFailed", path.c_str());
                continue;
            }
            storage_->RegisterModule(module->internalName, module->name);
            diagnostics_.Event("ScriptModuleLoaded", module->name.c_str());
            modules_.push_back(std::move(module));
        }
        if (modules_.empty())
        {
            diagnostics_.Log("AngelScript: every discovered module failed to compile.");
            return false;
        }
        return true;
    }

    std::unique_ptr<ScriptHost::ScriptModule> ScriptHost::BuildModule(
        const std::filesystem::path& scriptPath,
        std::size_t moduleIndex)
    {
        std::ifstream input(scriptPath, std::ios::binary);
        if (!input)
        {
            diagnostics_.Log("AngelScript: a deployed script module could not be opened.");
            return nullptr;
        }
        const std::string source{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
        if (source.empty())
        {
            diagnostics_.Log("AngelScript: a deployed script module is empty.");
            return nullptr;
        }

        std::ostringstream moduleName;
        moduleName << "FableTogether.Mod." << moduleIndex;
        const std::string name = moduleName.str();
        const std::string section = PathToUtf8(scriptPath.filename());
        asIScriptModule* module = engine_->GetModule(name.c_str(), asGM_ALWAYS_CREATE);
        if (module == nullptr ||
            module->AddScriptSection(section.c_str(), source.c_str(), source.size()) < 0 ||
            module->Build() < 0)
        {
            diagnostics_.Log("AngelScript: module compilation failed.");
            return nullptr;
        }

        auto result = std::make_unique<ScriptModule>();
        result->name = PathToUtf8(scriptPath.lexically_relative(scriptsRoot_));
        result->internalName = name;
        result->path = scriptPath;
        result->onLoad = module->GetFunctionByDecl("void OnLoad()");
        if (result->onLoad == nullptr)
        {
            result->onLoad = module->GetFunctionByDecl("void OnStart()");
        }
        result->onUnload = module->GetFunctionByDecl("void OnUnload()");
        result->onKeyPressed = module->GetFunctionByDecl("void OnKeyPressed(uint, bool)");
        result->onWorldReady = module->GetFunctionByDecl("void OnWorldReady()");
        result->onTick = module->GetFunctionByDecl("void OnTick(float)");
        return result;
    }

    bool ScriptHost::Execute(asIScriptFunction* function)
    {
        if (engine_ == nullptr || function == nullptr)
        {
            return false;
        }

        asIScriptContext* context = engine_->CreateContext();
        if (context == nullptr)
        {
            diagnostics_.Log("AngelScript: execution context allocation failed.");
            return false;
        }

        const int prepareResult = context->Prepare(function);
        const int executionResult = prepareResult >= 0
            ? context->Execute()
            : prepareResult;
        if (executionResult != asEXECUTION_FINISHED)
        {
            ReportExecutionFailure(function->GetName(), executionResult);
        }
        context->Release();
        return executionResult == asEXECUTION_FINISHED;
    }

    void ScriptHost::DispatchKeyPressed(unsigned int virtualKey, bool shiftPressed)
    {
        if (!loaded_)
        {
            return;
        }

        for (const auto& module : modules_)
        {
            if (!module->enabled || module->onKeyPressed == nullptr)
            {
                continue;
            }
            asIScriptContext* context = engine_->CreateContext();
            if (context == nullptr || context->Prepare(module->onKeyPressed) < 0)
            {
                if (context != nullptr)
                {
                    context->Release();
                }
                diagnostics_.Log("AngelScript: OnKeyPressed context preparation failed.");
                continue;
            }
            context->SetArgDWord(0, virtualKey);
            context->SetArgByte(1, shiftPressed ? 1 : 0);
            const int result = context->Execute();
            if (result != asEXECUTION_FINISHED)
            {
                ReportExecutionFailure("OnKeyPressed", result);
                module->enabled = false;
                diagnostics_.Event("ScriptModuleDisabled", module->name.c_str());
            }
            context->Release();
        }
    }

    void ScriptHost::DispatchWorldReady()
    {
        if (!loaded_)
        {
            return;
        }
        if (!multiplayerSession_->OnWorldReady())
        {
            diagnostics_.Event("ClientFailed", "multiplayer-world-entry");
            return;
        }
        for (const auto& module : modules_)
        {
            if (!module->enabled || module->onWorldReady == nullptr)
            {
                continue;
            }
            if (!Execute(module->onWorldReady))
            {
                module->enabled = false;
                diagnostics_.Event("ScriptModuleDisabled", module->name.c_str());
            }
        }
        events_->Emit("WorldReady", "native world and Hero state are stable");
    }

    void ScriptHost::Tick(float deltaSeconds)
    {
        if (!loaded_)
        {
            return;
        }

        scheduler_->Tick(deltaSeconds);
        creatureLocomotionService_->TickHeroShadow();
        npcTransferAcceptanceDriver_->Tick(
            multiplayerSession_->HasActiveRemotePresentation());
        transitionAcceptanceDriver_->Tick(
            deltaSeconds,
            multiplayerSession_->HasActiveRemotePresentation());
        combatTargetAcceptanceDriver_->Tick(
            multiplayerSession_->HasActiveRemotePresentation());

        for (const auto& module : modules_)
        {
            if (!module->enabled || module->onTick == nullptr)
            {
                continue;
            }
            asIScriptContext* context = engine_->CreateContext();
            if (context == nullptr || context->Prepare(module->onTick) < 0)
            {
                if (context != nullptr)
                {
                    context->Release();
                }
                diagnostics_.Log("AngelScript: OnTick context preparation failed.");
                continue;
            }
            context->SetArgFloat(0, deltaSeconds);
            const int result = context->Execute();
            if (result != asEXECUTION_FINISHED)
            {
                ReportExecutionFailure("OnTick", result);
                module->enabled = false;
                diagnostics_.Event("ScriptModuleDisabled", module->name.c_str());
            }
            context->Release();
        }
    }

    bool ScriptHost::ProcessMultiplayerPresentation()
    {
        if (!loaded_)
        {
            return false;
        }
        // Creature creation and retirement must execute from Fable's verified
        // game-thread queue boundary. Network state is still received by the
        // transport worker and movement is still consumed by creature hooks;
        // this drains only the single bounded lifecycle delta.
        return multiplayerSession_->ProcessPresentationLifecycle();
    }

    void ScriptHost::DriveReplicatedMovement()
    {
        if (loaded_)
        {
            multiplayerSession_->DriveReplicatedMovement();
        }
    }

    bool ScriptHost::Reload()
    {
        if (multiplayerSession_->IsEnabled())
        {
            diagnostics_.Event(
                "ScriptReloadSkipped",
                "multiplayer owns shared locomotion and combat routes");
            return false;
        }
        if (engine_ == nullptr || scriptsRoot_.empty())
        {
            return false;
        }

        creatureCombatService_->ClearPlayerCombat();
        creatureLocomotionService_->ClearHeroShadow();

        for (auto iterator = modules_.rbegin(); iterator != modules_.rend(); ++iterator)
        {
            if ((*iterator)->enabled && (*iterator)->onUnload != nullptr)
            {
                Execute((*iterator)->onUnload);
            }
        }
        events_->UnsubscribeAll();
        scheduler_->CancelAll();
        storage_->ClearModules();
        for (const auto& module : modules_)
        {
            engine_->DiscardModule(module->internalName.c_str());
        }
        modules_.clear();
        loaded_ = false;

        if (!LoadModules(scriptsRoot_))
        {
            diagnostics_.Event("ScriptReloadFailed", "module discovery or compilation failed");
            return false;
        }
        loaded_ = true;
        bool anyEnabled = false;
        for (const auto& module : modules_)
        {
            if (module->onLoad != nullptr && !Execute(module->onLoad))
            {
                module->enabled = false;
                diagnostics_.Event("ScriptModuleDisabled", module->name.c_str());
                continue;
            }
            anyEnabled = true;
        }
        loaded_ = anyEnabled;
        diagnostics_.Event(
            loaded_ ? "ScriptReloaded" : "ScriptReloadFailed",
            loaded_ ? "all deployed AngelScript modules recompiled" : "no module completed its load callback");
        return loaded_;
    }

    void ScriptHost::ReportExecutionFailure(const char* callbackName, int executionResult)
    {
        char message[256] = {};
        std::snprintf(
            message,
            sizeof(message),
            "AngelScript: %s execution failed with result %d.",
            callbackName,
            executionResult);
        diagnostics_.Log(message);
    }

    void ScriptHost::Shutdown()
    {
        npcTransferAcceptanceDriver_->Shutdown();
        combatTargetAcceptanceDriver_->Shutdown();
        transitionAcceptanceDriver_->Shutdown();
        multiplayerSession_->Shutdown();
        creatureCombatService_->ClearPlayerCombat();
        creatureLocomotionService_->ClearHeroShadow();
        if (loaded_)
        {
            for (auto iterator = modules_.rbegin(); iterator != modules_.rend(); ++iterator)
            {
                if ((*iterator)->onUnload != nullptr)
                {
                    Execute((*iterator)->onUnload);
                }
            }
        }
        loaded_ = false;
        events_->Shutdown();
        storage_->Shutdown();
        scheduler_->Shutdown();
        modules_.clear();
        if (engine_ != nullptr)
        {
            engine_->ShutDownAndRelease();
            engine_ = nullptr;
        }
        diagnostics_ = {};
        g_diagnostics = {};
        scriptsRoot_.clear();
    }

    bool ScriptHost::IsLoaded() const noexcept
    {
        return loaded_;
    }
}
