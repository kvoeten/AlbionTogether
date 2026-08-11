#include "PersistentStore.h"

#include <Windows.h>
#include <angelscript.h>

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace
{
    constexpr wchar_t kSectionName[] = L"Values";

    std::wstring Utf8ToWide(const std::string& value)
    {
        if (value.empty())
        {
            return {};
        }
        const int required = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0);
        if (required <= 0)
        {
            return {};
        }
        std::wstring result(static_cast<std::size_t>(required), L'\0');
        if (MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                static_cast<int>(value.size()), result.data(), required) != required)
        {
            return {};
        }
        return result;
    }

    std::string WideToUtf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }
        const int required = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (required <= 0)
        {
            return {};
        }
        std::string result(static_cast<std::size_t>(required), '\0');
        if (WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                static_cast<int>(value.size()), result.data(), required,
                nullptr, nullptr) != required)
        {
            return {};
        }
        return result;
    }

    std::uint32_t StableHash(const std::string& value)
    {
        std::uint32_t hash = 2166136261u;
        for (const unsigned char character : value)
        {
            hash ^= character;
            hash *= 16777619u;
        }
        return hash;
    }

    std::string SafeFileStem(const std::string& value)
    {
        std::string result;
        result.reserve(value.size());
        for (const unsigned char character : value)
        {
            const bool alphaNumeric =
                (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9');
            result.push_back(alphaNumeric || character == '-' || character == '_'
                ? static_cast<char>(character)
                : '_');
        }
        if (result.empty())
        {
            result = "module";
        }
        if (result.size() > 80)
        {
            result.resize(80);
        }
        char suffix[16] = {};
        std::snprintf(suffix, sizeof(suffix), "_%08X", StableHash(value));
        result += suffix;
        return result;
    }

    std::string HexEncode(const std::string& value)
    {
        constexpr char digits[] = "0123456789ABCDEF";
        std::string result;
        result.reserve(value.size() * 2);
        for (const unsigned char character : value)
        {
            result.push_back(digits[character >> 4]);
            result.push_back(digits[character & 0x0F]);
        }
        return result;
    }

    int HexDigit(char value)
    {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        return -1;
    }

    bool HexDecode(const std::string& value, std::string& result)
    {
        if ((value.size() & 1u) != 0)
        {
            return false;
        }
        result.clear();
        result.reserve(value.size() / 2);
        for (std::size_t index = 0; index < value.size(); index += 2)
        {
            const int high = HexDigit(value[index]);
            const int low = HexDigit(value[index + 1]);
            if (high < 0 || low < 0)
            {
                result.clear();
                return false;
            }
            result.push_back(static_cast<char>((high << 4) | low));
        }
        return true;
    }
}

namespace fable::scripting
{
    void PersistentStore::Initialize(
        asIScriptEngine& engine,
        const std::filesystem::path& root,
        const core::Diagnostics& diagnostics)
    {
        Shutdown();
        engine_ = &engine;
        root_ = root;
        diagnostics_ = diagnostics;
        std::error_code error;
        std::filesystem::create_directories(root_, error);
        if (error)
        {
            diagnostics_.Log("AngelScript storage: persistent data directory could not be created.");
        }
    }

    void PersistentStore::RegisterModule(
        const std::string& internalName,
        const std::string& stableRelativePath)
    {
        if (internalName.empty() || stableRelativePath.empty() || root_.empty())
        {
            return;
        }
        moduleFiles_[internalName] =
            root_ / (SafeFileStem(stableRelativePath) + ".ini");
    }

    void PersistentStore::ClearModules()
    {
        moduleFiles_.clear();
    }

    bool PersistentStore::ResolveActiveFile(std::filesystem::path& file) const
    {
        asIScriptContext* context = asGetActiveContext();
        asIScriptFunction* function = context != nullptr ? context->GetFunction() : nullptr;
        const char* moduleName = function != nullptr ? function->GetModuleName() : nullptr;
        if (moduleName == nullptr)
        {
            return false;
        }
        const auto found = moduleFiles_.find(moduleName);
        if (found == moduleFiles_.end())
        {
            return false;
        }
        file = found->second;
        return true;
    }

    bool PersistentStore::IsValidKey(const std::string& key) const
    {
        if (key.empty() || key.size() > 128)
        {
            return false;
        }
        for (const unsigned char character : key)
        {
            const bool alphaNumeric =
                (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9');
            if (!alphaNumeric && character != '.' && character != '_' &&
                character != '-' && character != ':')
            {
                return false;
            }
        }
        return true;
    }

    bool PersistentStore::ReadRaw(const std::string& key, std::string& value) const
    {
        value.clear();
        std::filesystem::path file;
        if (!IsValidKey(key) || !ResolveActiveFile(file))
        {
            return false;
        }
        const std::wstring wideKey = Utf8ToWide(key);
        if (wideKey.empty())
        {
            return false;
        }
        std::vector<wchar_t> buffer(16'385, L'\0');
        const DWORD length = GetPrivateProfileStringW(
            kSectionName,
            wideKey.c_str(),
            L"",
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            file.c_str());
        if (length == 0 || length >= buffer.size() - 1)
        {
            return false;
        }
        value = WideToUtf8(std::wstring(buffer.data(), length));
        return !value.empty();
    }

    bool PersistentStore::WriteRaw(const std::string& key, const std::string* value)
    {
        std::filesystem::path file;
        if (!IsValidKey(key) || !ResolveActiveFile(file))
        {
            return false;
        }
        const std::wstring wideKey = Utf8ToWide(key);
        const std::wstring wideValue = value != nullptr ? Utf8ToWide(*value) : std::wstring{};
        if (wideKey.empty() || (value != nullptr && wideValue.empty()))
        {
            return false;
        }
        return WritePrivateProfileStringW(
            kSectionName,
            wideKey.c_str(),
            value != nullptr ? wideValue.c_str() : nullptr,
            file.c_str()) != FALSE;
    }

    bool PersistentStore::Has(const std::string& key) const
    {
        std::string value;
        return ReadRaw(key, value);
    }

    std::string PersistentStore::GetString(
        const std::string& key,
        const std::string& fallback) const
    {
        std::string raw;
        std::string value;
        return ReadRaw(key, raw) && raw.rfind("s:", 0) == 0 &&
            HexDecode(raw.substr(2), value)
            ? value
            : fallback;
    }

    std::int64_t PersistentStore::GetInteger(
        const std::string& key,
        std::int64_t fallback) const
    {
        std::string raw;
        if (!ReadRaw(key, raw) || raw.rfind("i:", 0) != 0)
        {
            return fallback;
        }
        char* end = nullptr;
        errno = 0;
        const long long value = std::strtoll(raw.c_str() + 2, &end, 10);
        return errno == 0 && end != nullptr && *end == '\0'
            ? static_cast<std::int64_t>(value)
            : fallback;
    }

    double PersistentStore::GetNumber(
        const std::string& key,
        double fallback) const
    {
        std::string raw;
        if (!ReadRaw(key, raw) || raw.rfind("n:", 0) != 0)
        {
            return fallback;
        }
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(raw.c_str() + 2, &end);
        return errno == 0 && end != nullptr && *end == '\0' && std::isfinite(value)
            ? value
            : fallback;
    }

    bool PersistentStore::GetBoolean(const std::string& key, bool fallback) const
    {
        std::string raw;
        if (!ReadRaw(key, raw))
        {
            return fallback;
        }
        if (raw == "b:1") return true;
        if (raw == "b:0") return false;
        return fallback;
    }

    bool PersistentStore::SetString(const std::string& key, const std::string& value)
    {
        if (value.size() > 8'000)
        {
            return false;
        }
        const std::string encoded = "s:" + HexEncode(value);
        return WriteRaw(key, &encoded);
    }

    bool PersistentStore::SetInteger(const std::string& key, std::int64_t value)
    {
        const std::string encoded = "i:" + std::to_string(value);
        return WriteRaw(key, &encoded);
    }

    bool PersistentStore::SetNumber(const std::string& key, double value)
    {
        if (!std::isfinite(value))
        {
            return false;
        }
        char text[64] = {};
        std::snprintf(text, sizeof(text), "n:%.17g", value);
        const std::string encoded(text);
        return WriteRaw(key, &encoded);
    }

    bool PersistentStore::SetBoolean(const std::string& key, bool value)
    {
        const std::string encoded = value ? "b:1" : "b:0";
        return WriteRaw(key, &encoded);
    }

    bool PersistentStore::Remove(const std::string& key)
    {
        return WriteRaw(key, nullptr);
    }

    bool PersistentStore::Flush() const
    {
        std::filesystem::path file;
        if (!ResolveActiveFile(file))
        {
            return false;
        }
        // Ask the profile API to release any cached view, then issue a real
        // filesystem flush whose result is unambiguous to scripts.
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, file.c_str());
        const HANDLE handle = CreateFileW(
            file.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        const bool flushed = FlushFileBuffers(handle) != FALSE;
        CloseHandle(handle);
        return flushed;
    }

    void PersistentStore::Shutdown()
    {
        moduleFiles_.clear();
        engine_ = nullptr;
        root_.clear();
        diagnostics_ = {};
    }
}
