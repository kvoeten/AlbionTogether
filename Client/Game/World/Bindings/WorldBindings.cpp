#include "WorldBindings.h"

#include "Core/Capabilities/CapabilityRegistry.h"
#include "Game/World/WorldService.h"

#include <angelscript.h>
#include <scriptarray.h>

#include <vector>

namespace
{
    fable::game::WorldService* g_worldService = nullptr;
    fable::core::CapabilityRegistry* g_capabilities = nullptr;
    fable::core::Diagnostics g_diagnostics;
    asIScriptEngine* g_engine = nullptr;

    fable::game::Entity* GetHero()
    {
        return g_worldService != nullptr ? g_worldService->GetHero() : nullptr;
    }

    fable::game::Entity* FindByScriptName(const std::string& scriptName)
    {
        return g_worldService != nullptr
            ? g_worldService->FindByScriptName(scriptName)
            : nullptr;
    }

    fable::game::Entity* CreateCreature(
        const std::string& definition,
        const fable::game::Vector3& position,
        const std::string& scriptName)
    {
        return g_worldService != nullptr
            ? g_worldService->CreateCreature(definition, position, scriptName)
            : nullptr;
    }

    bool HasCapability(const std::string& name)
    {
        return g_capabilities != nullptr && g_capabilities->IsAvailable(name);
    }

    bool IsVerifiedCapability(const std::string& name)
    {
        return g_capabilities != nullptr && g_capabilities->IsVerified(name);
    }

    int GetCapabilityStatus(const std::string& name)
    {
        return g_capabilities != nullptr
            ? static_cast<int>(g_capabilities->Status(name))
            : 0;
    }

    std::string DescribeCapability(const std::string& name)
    {
        return g_capabilities != nullptr
            ? g_capabilities->Describe(name)
            : std::string{};
    }

    CScriptArray* GetCapabilityNames()
    {
        if (g_engine == nullptr || g_capabilities == nullptr)
        {
            return nullptr;
        }
        asITypeInfo* const type = g_engine->GetTypeInfoByDecl("array<string>");
        if (type == nullptr)
        {
            return nullptr;
        }
        const std::vector<std::string> names = g_capabilities->Names();
        CScriptArray* const result = CScriptArray::Create(
            type,
            static_cast<asUINT>(names.size()));
        if (result == nullptr)
        {
            return nullptr;
        }
        for (asUINT index = 0; index < result->GetSize(); ++index)
        {
            *static_cast<std::string*>(result->At(index)) = names[index];
        }
        return result;
    }

    void DebugLog(const std::string& message)
    {
        g_diagnostics.Log(message.c_str());
    }

    void DebugEvent(const std::string& state, const std::string& detail)
    {
        g_diagnostics.Event(state.c_str(), detail.c_str());
    }
}

namespace fable::scripting::bindings
{
    bool RegisterWorldBindings(
        asIScriptEngine& engine,
        game::WorldService& worldService,
        core::CapabilityRegistry& capabilities,
        const core::Diagnostics& diagnostics)
    {
        g_worldService = &worldService;
        g_capabilities = &capabilities;
        g_diagnostics = diagnostics;
        g_engine = &engine;

        int result = engine.SetDefaultNamespace("World");
        result = result >= 0
            ? engine.RegisterGlobalFunction("Entity@+ GetHero()", asFUNCTION(GetHero), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "Entity@+ FindByScriptName(const string &in)",
                asFUNCTION(FindByScriptName),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "Entity@+ CreateCreature(const string &in, const Vector3 &in, const string &in scriptName = '')",
                asFUNCTION(CreateCreature),
                asCALL_CDECL)
            : result;

        result = result >= 0 ? engine.SetDefaultNamespace("Capabilities") : result;
        result = result >= 0 ? engine.RegisterEnum("CapabilityStatus") : result;
        result = result >= 0
            ? engine.RegisterEnumValue("CapabilityStatus", "Unavailable", 0)
            : result;
        result = result >= 0
            ? engine.RegisterEnumValue("CapabilityStatus", "Experimental", 1)
            : result;
        result = result >= 0
            ? engine.RegisterEnumValue("CapabilityStatus", "Verified", 2)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool IsAvailable(const string &in)",
                asFUNCTION(HasCapability),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool IsVerified(const string &in)",
                asFUNCTION(IsVerifiedCapability),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "CapabilityStatus GetStatus(const string &in)",
                asFUNCTION(GetCapabilityStatus),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "string Describe(const string &in)",
                asFUNCTION(DescribeCapability),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "array<string>@+ GetNames()",
                asFUNCTION(GetCapabilityNames),
                asCALL_CDECL)
            : result;

        result = result >= 0 ? engine.SetDefaultNamespace("Debug") : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "void Log(const string &in)",
                asFUNCTION(DebugLog),
                asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "void Event(const string &in, const string &in detail = '')",
                asFUNCTION(DebugEvent),
                asCALL_CDECL)
            : result;
        const int resetResult = engine.SetDefaultNamespace("");
        return result >= 0 && resetResult >= 0;
    }
}
