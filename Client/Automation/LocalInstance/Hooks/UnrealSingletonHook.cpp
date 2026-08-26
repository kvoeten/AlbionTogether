#include "UnrealSingletonHook.h"

#include <cstdio>
#include <cwchar>

namespace
{
    constexpr wchar_t kRetailMutexName[] = L"UnrealEngine3_8";

    std::string WideToUtf8(const wchar_t* value)
    {
        if (value == nullptr || *value == L'\0')
        {
            return {};
        }
        const int required = WideCharToMultiByte(
            CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1)
        {
            return {};
        }
        std::string result(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, value, -1, result.data(), required, nullptr, nullptr);
        result.pop_back();
        return result;
    }
}

namespace fable::automation::local_instance
{
    UnrealSingletonHook* UnrealSingletonHook::active_ = nullptr;

    bool UnrealSingletonHook::Install(
        HMODULE gameModule,
        const wchar_t* sessionId,
        const wchar_t* instanceId) noexcept
    {
        if (IsInstalled())
        {
            return true;
        }
        if (sessionId == nullptr || *sessionId == L'\0' ||
            instanceId == nullptr || *instanceId == L'\0' ||
            active_ != nullptr)
        {
            return false;
        }

        native::UnrealSingletonImport::Resolved imported = {};
        if (!native::UnrealSingletonImport::Resolve(gameModule, imported))
        {
            return false;
        }

        try
        {
            namespacedMutex_ = L"Local\\FableTogether.UnrealEngine3_8.";
            namespacedMutex_.append(sessionId);
            namespacedMutex_.push_back(L'.');
            namespacedMutex_.append(instanceId);
        }
        catch (...)
        {
            namespacedMutex_.clear();
            return false;
        }

        original_ = imported.importedFunction;
        native::UnrealSingletonImport::Function replacement =
            &UnrealSingletonHook::CreateMutexRedirect;
        if (!patch_.Install(
                imported.slot,
                &imported.importedFunction,
                sizeof(*imported.slot),
                &replacement,
                sizeof(replacement)))
        {
            original_ = nullptr;
            namespacedMutex_.clear();
            return false;
        }
        active_ = this;
        return true;
    }

    void UnrealSingletonHook::Shutdown() noexcept
    {
        if (!patch_.Shutdown())
        {
            return;
        }
        if (active_ == this) active_ = nullptr;
        original_ = nullptr;
        namespacedMutex_.clear();
    }

    void UnrealSingletonHook::Report(const core::Diagnostics& diagnostics) const
    {
        if (!IsInstalled())
        {
            diagnostics.Log(
                "Local instance: Unreal singleton namespace hook is not installed.");
            diagnostics.Event(
                "ClientFailed",
                "local-instance-unreal-singleton-hook-missing");
            return;
        }

        const std::string mutexName = WideToUtf8(namespacedMutex_.c_str());
        char message[512] = {};
        std::snprintf(
            message,
            sizeof(message),
            "Local instance: exact UE3 mutex %s is namespaced; redirects=%lu.",
            mutexName.c_str(),
            RedirectCount());
        diagnostics.Log(message);
        diagnostics.Event("UnrealSingletonNamespaced", mutexName.c_str());
        if (RedirectCount() > 0)
        {
            diagnostics.Event("UnrealSingletonRedirected", mutexName.c_str());
        }
    }

    bool UnrealSingletonHook::IsInstalled() const noexcept
    {
        return active_ == this && original_ != nullptr && patch_.IsInstalled() &&
            !namespacedMutex_.empty();
    }

    const std::wstring& UnrealSingletonHook::NamespacedMutex() const noexcept
    {
        return namespacedMutex_;
    }

    unsigned long UnrealSingletonHook::RedirectCount() const noexcept
    {
        return redirectCount_.load(std::memory_order_acquire);
    }

    HANDLE WINAPI UnrealSingletonHook::CreateMutexRedirect(
        LPSECURITY_ATTRIBUTES attributes,
        BOOL initialOwner,
        LPCWSTR name)
    {
        UnrealSingletonHook* const hook = active_;
        if (hook == nullptr || hook->original_ == nullptr)
        {
            SetLastError(ERROR_INVALID_FUNCTION);
            return nullptr;
        }

        if (name != nullptr &&
            std::wcscmp(name, kRetailMutexName) == 0 &&
            !hook->namespacedMutex_.empty())
        {
            hook->redirectCount_.fetch_add(1, std::memory_order_acq_rel);
            return hook->original_(
                attributes,
                initialOwner,
                hook->namespacedMutex_.c_str());
        }
        return hook->original_(attributes, initialOwner, name);
    }
}
