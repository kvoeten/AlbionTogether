#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

class asIScriptEngine;

namespace fable::scripting
{
    class PersistentStore final
    {
    public:
        void Initialize(
            asIScriptEngine& engine,
            const std::filesystem::path& root,
            const core::Diagnostics& diagnostics);
        void RegisterModule(
            const std::string& internalName,
            const std::string& stableRelativePath);
        void ClearModules();
        void Shutdown();

        [[nodiscard]] bool Has(const std::string& key) const;
        [[nodiscard]] std::string GetString(
            const std::string& key,
            const std::string& fallback) const;
        [[nodiscard]] std::int64_t GetInteger(
            const std::string& key,
            std::int64_t fallback) const;
        [[nodiscard]] double GetNumber(
            const std::string& key,
            double fallback) const;
        [[nodiscard]] bool GetBoolean(
            const std::string& key,
            bool fallback) const;

        bool SetString(const std::string& key, const std::string& value);
        bool SetInteger(const std::string& key, std::int64_t value);
        bool SetNumber(const std::string& key, double value);
        bool SetBoolean(const std::string& key, bool value);
        bool Remove(const std::string& key);
        bool Flush() const;

    private:
        [[nodiscard]] bool ResolveActiveFile(std::filesystem::path& file) const;
        [[nodiscard]] bool ReadRaw(const std::string& key, std::string& value) const;
        bool WriteRaw(const std::string& key, const std::string* value);
        [[nodiscard]] bool IsValidKey(const std::string& key) const;

        asIScriptEngine* engine_ = nullptr;
        std::filesystem::path root_;
        core::Diagnostics diagnostics_ = {};
        std::unordered_map<std::string, std::filesystem::path> moduleFiles_;
    };
}
