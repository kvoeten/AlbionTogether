#include "DeveloperToolBindings.h"

#include "DeveloperTools/DeveloperToolBackend.h"
#include "Scripting/Bindings/Registry/ScriptBindingRegistry.h"

#include <angelscript.h>

#include <atomic>
#include <cstdio>
#include <string>

namespace
{
    std::atomic<fable::developer_tools::DeveloperToolBackend*> g_backend{nullptr};
    std::atomic<bool> g_hostAuthorized{false};

    fable::developer_tools::DeveloperToolBackend* Backend() noexcept
    {
        return g_backend.load(std::memory_order_acquire);
    }

    bool IsHost() noexcept
    {
        return g_hostAuthorized.load(std::memory_order_acquire);
    }

    bool SpawnEntity(
        const std::string& definition,
        float x,
        float y,
        float z) noexcept
    {
        auto* const backend = Backend();
        return IsHost() && backend != nullptr &&
            backend->QueueSpawnEntity(
                fable::developer_tools::DeveloperToolText::From(
                    definition.c_str()),
                {x, y, z});
    }

    bool TeleportEntity(
        std::uint64_t uid,
        float x,
        float y,
        float z) noexcept
    {
        auto* const backend = Backend();
        return IsHost() && backend != nullptr &&
            backend->QueueTeleportEntity(uid, {x, y, z});
    }

    bool UseRegionExit(std::uint64_t uid) noexcept
    {
        auto* const backend = Backend();
        return IsHost() && backend != nullptr &&
            backend->QueueUseRegionExit(uid);
    }

    bool QueryQuest(const std::string& name) noexcept
    {
        auto* const backend = Backend();
        return IsHost() && backend != nullptr &&
            backend->QueueQuestQuery(
                fable::developer_tools::DeveloperToolText::From(name.c_str()));
    }

    bool ActivateQuest(const std::string& name) noexcept
    {
        auto* const backend = Backend();
        return IsHost() && backend != nullptr &&
            backend->QueueQuestActivation(
                fable::developer_tools::DeveloperToolText::From(name.c_str()));
    }

    bool QuerySaveSection(int section) noexcept
    {
        if (section < static_cast<int>(
                fable::developer_tools::DeveloperSaveSection::Entities) ||
            section > static_cast<int>(
                fable::developer_tools::DeveloperSaveSection::Factions))
        {
            return false;
        }
        auto* const backend = Backend();
        return IsHost() && backend != nullptr &&
            backend->QueueSaveSectionQuery(
                static_cast<fable::developer_tools::DeveloperSaveSection>(
                    section));
    }

    bool PollResult(std::string& detail, std::uint64_t& entityUid) noexcept
    {
        auto* const backend = Backend();
        fable::developer_tools::DeveloperToolResult result;
        if (backend == nullptr || !backend->TryTakeResult(result))
        {
            return false;
        }

        entityUid = result.entityUid;
        char formatted[256] = {};
        if (result.entityUid != 0U)
        {
            std::snprintf(
                formatted,
                sizeof(formatted),
                "%s (uid=%llu)",
                result.detail.value.data(),
                static_cast<unsigned long long>(result.entityUid));
            detail = formatted;
        }
        else
        {
            detail = result.detail.value.data();
        }
        return true;
    }

    std::uint32_t PendingCount() noexcept
    {
        auto* const backend = Backend();
        return backend != nullptr
            ? static_cast<std::uint32_t>(backend->PendingCount())
            : 0U;
    }
}

namespace fable::developer_tools::scripting
{
    void BindDeveloperToolApi(
        DeveloperToolBackend* const backend,
        const bool hostAuthorized) noexcept
    {
        g_hostAuthorized.store(hostAuthorized, std::memory_order_release);
        g_backend.store(backend, std::memory_order_release);
    }

    bool RegisterDeveloperToolBindingGroup(
        fable::scripting::bindings::BindingContext& context)
    {
        return RegisterDeveloperToolBindings(context.Engine);
    }

    bool RegisterDeveloperToolBindings(asIScriptEngine& engine)
    {
        int result = engine.RegisterEnum("SaveSection");
        result = result >= 0 ? engine.RegisterEnumValue(
            "SaveSection", "Entities", 0) : result;
        result = result >= 0 ? engine.RegisterEnumValue(
            "SaveSection", "Player", 1) : result;
        result = result >= 0 ? engine.RegisterEnumValue(
            "SaveSection", "Quests", 2) : result;
        result = result >= 0 ? engine.RegisterEnumValue(
            "SaveSection", "Regions", 3) : result;
        result = result >= 0 ? engine.RegisterEnumValue(
            "SaveSection", "Factions", 4) : result;
        result = result >= 0 ? engine.SetDefaultNamespace("DevTools") : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "bool IsHost()",
            asFUNCTION(IsHost), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "bool SpawnEntity(const string &in definition, float x, float y, float z)",
            asFUNCTION(SpawnEntity), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "bool TeleportEntity(uint64 uid, float x, float y, float z)",
            asFUNCTION(TeleportEntity), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "bool UseRegionExit(uint64 uid)",
            asFUNCTION(UseRegionExit), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "bool QueryQuest(const string &in name)",
            asFUNCTION(QueryQuest), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "bool ActivateQuest(const string &in name)",
            asFUNCTION(ActivateQuest), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "bool QuerySaveSection(SaveSection section)",
            asFUNCTION(QuerySaveSection), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "bool PollResult(string &out detail, uint64 &out entityUid)",
            asFUNCTION(PollResult), asCALL_CDECL) : result;
        result = result >= 0 ? engine.RegisterGlobalFunction(
            "uint PendingCount()",
            asFUNCTION(PendingCount), asCALL_CDECL) : result;
        const int resetResult = engine.SetDefaultNamespace("");
        return result >= 0 && resetResult >= 0;
    }
}

FABLE_SCRIPT_BINDING_GROUP(
    DeveloperTools,
    360,
    &fable::developer_tools::scripting::RegisterDeveloperToolBindingGroup);
