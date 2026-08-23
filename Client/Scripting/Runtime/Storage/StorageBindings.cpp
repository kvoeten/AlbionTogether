#include "StorageBindings.h"
#include "Scripting/Bindings/Registry/ScriptBindingRegistry.h"

#include "PersistentStore.h"

#include <angelscript.h>

#include <string>

namespace
{
    fable::scripting::PersistentStore* g_storage = nullptr;

    bool Has(const std::string& key)
    {
        return g_storage != nullptr && g_storage->Has(key);
    }

    std::string GetString(const std::string& key, const std::string& fallback)
    {
        return g_storage != nullptr ? g_storage->GetString(key, fallback) : fallback;
    }

    asINT64 GetInteger(const std::string& key, asINT64 fallback)
    {
        return g_storage != nullptr ? g_storage->GetInteger(key, fallback) : fallback;
    }

    double GetNumber(const std::string& key, double fallback)
    {
        return g_storage != nullptr ? g_storage->GetNumber(key, fallback) : fallback;
    }

    bool GetBoolean(const std::string& key, bool fallback)
    {
        return g_storage != nullptr ? g_storage->GetBoolean(key, fallback) : fallback;
    }

    bool SetString(const std::string& key, const std::string& value)
    {
        return g_storage != nullptr && g_storage->SetString(key, value);
    }

    bool SetInteger(const std::string& key, asINT64 value)
    {
        return g_storage != nullptr && g_storage->SetInteger(key, value);
    }

    bool SetNumber(const std::string& key, double value)
    {
        return g_storage != nullptr && g_storage->SetNumber(key, value);
    }

    bool SetBoolean(const std::string& key, bool value)
    {
        return g_storage != nullptr && g_storage->SetBoolean(key, value);
    }

    bool Remove(const std::string& key)
    {
        return g_storage != nullptr && g_storage->Remove(key);
    }

    bool Flush()
    {
        return g_storage != nullptr && g_storage->Flush();
    }
}

namespace fable::scripting::bindings
{
    bool RegisterStorageBindingGroup(BindingContext& context)
    {
        return context.Storage != nullptr &&
            RegisterStorageBindings(context.Engine, *context.Storage);
    }

    FABLE_SCRIPT_BINDING_GROUP(Storage, 420, &RegisterStorageBindingGroup);

    bool RegisterStorageBindings(asIScriptEngine& engine, PersistentStore& storage)
    {
        g_storage = &storage;
        int result = engine.SetDefaultNamespace("Storage");
        result = result >= 0
            ? engine.RegisterGlobalFunction("bool Has(const string &in key)", asFUNCTION(Has), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "string GetString(const string &in key, const string &in fallback = '')",
                asFUNCTION(GetString), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "int64 GetInteger(const string &in key, int64 fallback = 0)",
                asFUNCTION(GetInteger), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "double GetNumber(const string &in key, double fallback = 0.0)",
                asFUNCTION(GetNumber), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool GetBoolean(const string &in key, bool fallback = false)",
                asFUNCTION(GetBoolean), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool SetString(const string &in key, const string &in value)",
                asFUNCTION(SetString), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool SetInteger(const string &in key, int64 value)",
                asFUNCTION(SetInteger), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool SetNumber(const string &in key, double value)",
                asFUNCTION(SetNumber), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction(
                "bool SetBoolean(const string &in key, bool value)",
                asFUNCTION(SetBoolean), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction("bool Remove(const string &in key)", asFUNCTION(Remove), asCALL_CDECL)
            : result;
        result = result >= 0
            ? engine.RegisterGlobalFunction("bool Flush()", asFUNCTION(Flush), asCALL_CDECL)
            : result;
        engine.SetDefaultNamespace("");
        return result >= 0;
    }
}
