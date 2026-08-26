#include "CompiledDefinitionsRedirectHook.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <limits>
#include <utility>

namespace
{
    bool IsGameDefinitionsPath(const wchar_t* value)
    {
        if (value == nullptr)
        {
            return false;
        }
        const wchar_t* const slash = std::wcsrchr(value, L'/');
        const wchar_t* const backslash = std::wcsrchr(value, L'\\');
        const wchar_t* const separator = slash == nullptr
            ? backslash
            : backslash == nullptr || slash > backslash ? slash : backslash;
        const wchar_t* const fileName = separator == nullptr ? value : separator + 1;
        return _wcsicmp(fileName, L"game.bin") == 0 ||
            _wcsicmp(fileName, L"gamehard.bin") == 0;
    }

    bool IsGameDefinitionsPath(const char* value)
    {
        if (value == nullptr)
        {
            return false;
        }
        const char* const slash = std::strrchr(value, '/');
        const char* const backslash = std::strrchr(value, '\\');
        const char* const separator = slash == nullptr
            ? backslash
            : backslash == nullptr || slash > backslash ? slash : backslash;
        const char* const fileName = separator == nullptr ? value : separator + 1;
        return _stricmp(fileName, "game.bin") == 0 ||
            _stricmp(fileName, "gamehard.bin") == 0;
    }

    bool IsGameDefinitionsPath(const UNICODE_STRING* value) noexcept
    {
        __try
        {
            if (value == nullptr || value->Buffer == nullptr ||
                value->Length == 0 ||
                value->Length % sizeof(wchar_t) != 0)
            {
                return false;
            }

            const std::size_t length =
                value->Length / sizeof(wchar_t);
            std::size_t fileNameOffset = length;
            while (fileNameOffset > 0)
            {
                const wchar_t current = value->Buffer[fileNameOffset - 1];
                if (current == L'/' || current == L'\\')
                {
                    break;
                }
                --fileNameOffset;
            }
            constexpr wchar_t kGameDefinitionsFileName[] = L"game.bin";
            constexpr wchar_t kHardGameDefinitionsFileName[] = L"gamehard.bin";
            const std::size_t fileNameLength = length - fileNameOffset;
            const wchar_t* expected = nullptr;
            if (fileNameLength ==
                (sizeof(kGameDefinitionsFileName) / sizeof(wchar_t)) - 1)
            {
                expected = kGameDefinitionsFileName;
            }
            else if (fileNameLength ==
                (sizeof(kHardGameDefinitionsFileName) / sizeof(wchar_t)) - 1)
            {
                expected = kHardGameDefinitionsFileName;
            }
            else
            {
                return false;
            }

            for (std::size_t index = 0; index < fileNameLength; ++index)
            {
                wchar_t actual = value->Buffer[fileNameOffset + index];
                if (actual >= L'A' && actual <= L'Z')
                {
                    actual = static_cast<wchar_t>(actual - L'A' + L'a');
                }
                if (actual != expected[index])
                {
                    return false;
                }
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

}

namespace fable::game::definitions
{
    CompiledDefinitionsRedirectHook* CompiledDefinitionsRedirectHook::active_ = nullptr;

    bool CompiledDefinitionsRedirectHook::InstallEarly(
        HMODULE,
        const wchar_t* gameDefinitionsPath) noexcept
    {
        if (gameDefinitionsPath == nullptr || gameDefinitionsPath[0] == L'\0')
        {
            return true;
        }
        if (IsInstalled())
        {
            return gameDefinitionsPath_ == gameDefinitionsPath;
        }
        if (active_ != nullptr && active_ != this)
        {
            return false;
        }
        if (active_ == this)
        {
            return false;
        }

        native::CreateFileFunctions::Resolved resolved = {};
        if (!native::CreateFileFunctions::Resolve(resolved))
        {
            return false;
        }

        const int required = WideCharToMultiByte(
            CP_ACP,
            0,
            gameDefinitionsPath,
            -1,
            nullptr,
            0,
            nullptr,
            nullptr);
        if (required <= 1)
        {
            return false;
        }
        std::string ansiPath(static_cast<std::size_t>(required), '\0');
        if (WideCharToMultiByte(
                CP_ACP,
                0,
                gameDefinitionsPath,
                -1,
                ansiPath.data(),
                required,
                nullptr,
                nullptr) != required)
        {
            return false;
        }
        ansiPath.resize(static_cast<std::size_t>(required - 1));

        const std::wstring ntPath =
            std::wstring(L"\\??\\") + gameDefinitionsPath;
        constexpr std::size_t kMaximumUnicodeStringBytes =
            (std::numeric_limits<USHORT>::max)();
        if ((ntPath.size() + 1) * sizeof(wchar_t) >
            kMaximumUnicodeStringBytes)
        {
            return false;
        }

        constexpr std::size_t kPatchCount = 4;
        std::array<void*, kPatchCount> targets = {
            reinterpret_cast<void*>(resolved.wideFunction),
            reinterpret_cast<void*>(resolved.ansiFunction),
            reinterpret_cast<void*>(resolved.nativeFunction),
            reinterpret_cast<void*>(resolved.nativeOpenFunction)};
        const std::array<const void*, kPatchCount> replacements = {
            reinterpret_cast<const void*>(
                &CompiledDefinitionsRedirectHook::CreateFileWide),
            reinterpret_cast<const void*>(
                &CompiledDefinitionsRedirectHook::CreateFileAnsi),
            reinterpret_cast<const void*>(
                &CompiledDefinitionsRedirectHook::CreateFileNative),
            reinterpret_cast<const void*>(
                &CompiledDefinitionsRedirectHook::OpenFileNative)};
        gameDefinitionsPath_ = gameDefinitionsPath;
        gameDefinitionsNtPath_ = ntPath;
        gameDefinitionsPathAnsi_ = std::move(ansiPath);
        originalWide_ = nullptr;
        originalAnsi_ = nullptr;
        originalNative_ = nullptr;
        originalNativeOpen_ = nullptr;
        active_ = this;
        for (std::size_t index = 0; index < kPatchCount; ++index)
        {
            if (!patches_[index].Install(
                    targets[index],
                    targets[index],
                    native::CreateFileFunctions::PrologueSize,
                    const_cast<void*>(replacements[index]),
                    native::CreateFileFunctions::PrologueSize))
            {
                bool rollbackRestored = true;
                for (std::size_t rollback = index; rollback > 0; --rollback)
                {
                    rollbackRestored =
                        patches_[rollback - 1].Shutdown() && rollbackRestored;
                }
                if (!rollbackRestored)
                {
                    diagnostics_.Log(
                        "Hook: compiled definitions rollback deferred because a target is owned by another hook.");
                    return false;
                }
                active_ = nullptr;
                originalWide_ = nullptr;
                originalAnsi_ = nullptr;
                originalNative_ = nullptr;
                originalNativeOpen_ = nullptr;
                gameDefinitionsPath_.clear();
                gameDefinitionsNtPath_.clear();
                gameDefinitionsPathAnsi_.clear();
                return false;
            }
            switch (index)
            {
            case 0:
                originalWide_ = reinterpret_cast<
                    native::CreateFileFunctions::WideFunction>(
                        patches_[index].Original());
                break;
            case 1:
                originalAnsi_ = reinterpret_cast<
                    native::CreateFileFunctions::AnsiFunction>(
                        patches_[index].Original());
                break;
            case 2:
                originalNative_ = reinterpret_cast<
                    native::CreateFileFunctions::NativeFunction>(
                        patches_[index].Original());
                break;
            case 3:
                originalNativeOpen_ = reinterpret_cast<
                    native::CreateFileFunctions::NativeOpenFunction>(
                        patches_[index].Original());
                break;
            default:
                break;
            }
        }
        originalWide_ = reinterpret_cast<native::CreateFileFunctions::WideFunction>(patches_[0].Original());
        originalAnsi_ = reinterpret_cast<native::CreateFileFunctions::AnsiFunction>(patches_[1].Original());
        originalNative_ = reinterpret_cast<native::CreateFileFunctions::NativeFunction>(patches_[2].Original());
        originalNativeOpen_ = reinterpret_cast<native::CreateFileFunctions::NativeOpenFunction>(patches_[3].Original());
        installed_ = true;
        return true;
    }

    void CompiledDefinitionsRedirectHook::Report(
        const core::Diagnostics& diagnostics)
    {
        diagnostics_ = diagnostics;
        if (!IsInstalled() ||
            (diagnostics_.log == nullptr && diagnostics_.event == nullptr))
        {
            return;
        }
        if (!readyReported_.exchange(true, std::memory_order_acq_rel))
        {
            char detail[1'024] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "path=%s;redirected=%s",
                gameDefinitionsPathAnsi_.c_str(),
                redirectOccurred_.load(std::memory_order_acquire) ? "true" : "false");
            diagnostics_.Log(
                "Hook: compiled game definitions redirect installed before the primary game thread resumed.");
            diagnostics_.Event("CompiledDefinitionsRedirectReady", detail);
        }
        if (redirectOccurred_.load(std::memory_order_acquire) &&
            !redirectReported_.exchange(true, std::memory_order_acq_rel))
        {
            diagnostics_.Log(
                "Definitions: redirected the selected retail game definitions to the remote-Hero sidecar.");
            diagnostics_.Event(
                "CompiledDefinitionsRedirected",
                gameDefinitionsPathAnsi_.c_str());
        }
    }

    void CompiledDefinitionsRedirectHook::Shutdown() noexcept
    {
        bool allRestored = true;
        for (std::size_t index = patches_.size(); index > 0; --index)
        {
            allRestored = patches_[index - 1].Shutdown() && allRestored;
        }
        if (!allRestored)
        {
            diagnostics_.Log("Hook: compiled definitions shutdown deferred because a target is owned by another hook.");
            return;
        }
        if (active_ == this) active_ = nullptr;
        originalWide_ = nullptr;
        originalAnsi_ = nullptr;
        originalNative_ = nullptr;
        originalNativeOpen_ = nullptr;
        gameDefinitionsPath_.clear();
        gameDefinitionsNtPath_.clear();
        gameDefinitionsPathAnsi_.clear();
        redirectOccurred_.store(false, std::memory_order_release);
        readyReported_.store(false, std::memory_order_release);
        redirectReported_.store(false, std::memory_order_release);
        diagnostics_ = {};
        installed_ = false;
    }

    bool CompiledDefinitionsRedirectHook::IsInstalled() const noexcept
    {
        return installed_ && active_ == this &&
            patches_[0].IsInstalled() && patches_[1].IsInstalled() &&
            patches_[2].IsInstalled() && patches_[3].IsInstalled() &&
            originalWide_ != nullptr && originalAnsi_ != nullptr &&
            originalNative_ != nullptr && originalNativeOpen_ != nullptr;
    }

    HANDLE WINAPI CompiledDefinitionsRedirectHook::CreateFileWide(
        LPCWSTR fileName,
        DWORD desiredAccess,
        DWORD shareMode,
        LPSECURITY_ATTRIBUTES securityAttributes,
        DWORD creationDisposition,
        DWORD flagsAndAttributes,
        HANDLE templateFile)
    {
        CompiledDefinitionsRedirectHook* const hook = active_;
        if (hook == nullptr || hook->originalWide_ == nullptr)
        {
            SetLastError(ERROR_INVALID_FUNCTION);
            return INVALID_HANDLE_VALUE;
        }
        const bool redirect = IsGameDefinitionsPath(fileName);
        if (redirect)
        {
            hook->redirectOccurred_.store(true, std::memory_order_release);
            hook->Report(hook->diagnostics_);
        }
        return hook->originalWide_(
            redirect ? hook->gameDefinitionsPath_.c_str() : fileName,
            desiredAccess,
            shareMode,
            securityAttributes,
            creationDisposition,
            flagsAndAttributes,
            templateFile);
    }

    HANDLE WINAPI CompiledDefinitionsRedirectHook::CreateFileAnsi(
        LPCSTR fileName,
        DWORD desiredAccess,
        DWORD shareMode,
        LPSECURITY_ATTRIBUTES securityAttributes,
        DWORD creationDisposition,
        DWORD flagsAndAttributes,
        HANDLE templateFile)
    {
        CompiledDefinitionsRedirectHook* const hook = active_;
        if (hook == nullptr || hook->originalAnsi_ == nullptr)
        {
            SetLastError(ERROR_INVALID_FUNCTION);
            return INVALID_HANDLE_VALUE;
        }
        const bool redirect = IsGameDefinitionsPath(fileName);
        if (redirect)
        {
            hook->redirectOccurred_.store(true, std::memory_order_release);
            hook->Report(hook->diagnostics_);
        }
        return hook->originalAnsi_(
            redirect ? hook->gameDefinitionsPathAnsi_.c_str() : fileName,
            desiredAccess,
            shareMode,
            securityAttributes,
            creationDisposition,
            flagsAndAttributes,
            templateFile);
    }

    NTSTATUS NTAPI CompiledDefinitionsRedirectHook::CreateFileNative(
        PHANDLE fileHandle,
        ACCESS_MASK desiredAccess,
        POBJECT_ATTRIBUTES objectAttributes,
        PIO_STATUS_BLOCK ioStatusBlock,
        PLARGE_INTEGER allocationSize,
        ULONG fileAttributes,
        ULONG shareAccess,
        ULONG createDisposition,
        ULONG createOptions,
        PVOID eaBuffer,
        ULONG eaLength)
    {
        CompiledDefinitionsRedirectHook* const hook = active_;
        if (hook == nullptr || hook->originalNative_ == nullptr)
        {
            return static_cast<NTSTATUS>(0xC000000DUL);
        }

        OBJECT_ATTRIBUTES redirectedAttributes = {};
        bool redirect = false;
        __try
        {
            if (objectAttributes != nullptr &&
                IsGameDefinitionsPath(objectAttributes->ObjectName))
            {
                redirectedAttributes = *objectAttributes;
                redirect = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            redirect = false;
        }

        UNICODE_STRING redirectedName = {};
        if (redirect)
        {
            const std::size_t pathBytes =
                hook->gameDefinitionsNtPath_.size() * sizeof(wchar_t);
            redirectedName.Buffer = const_cast<PWSTR>(
                hook->gameDefinitionsNtPath_.c_str());
            redirectedName.Length = static_cast<USHORT>(pathBytes);
            redirectedName.MaximumLength = static_cast<USHORT>(
                pathBytes + sizeof(wchar_t));
            redirectedAttributes.RootDirectory = nullptr;
            redirectedAttributes.ObjectName = &redirectedName;
            hook->redirectOccurred_.store(true, std::memory_order_release);
            hook->Report(hook->diagnostics_);
        }

        return hook->originalNative_(
            fileHandle,
            desiredAccess,
            redirect ? &redirectedAttributes : objectAttributes,
            ioStatusBlock,
            allocationSize,
            fileAttributes,
            shareAccess,
            createDisposition,
            createOptions,
            eaBuffer,
            eaLength);
    }

    NTSTATUS NTAPI CompiledDefinitionsRedirectHook::OpenFileNative(
        PHANDLE fileHandle,
        ACCESS_MASK desiredAccess,
        POBJECT_ATTRIBUTES objectAttributes,
        PIO_STATUS_BLOCK ioStatusBlock,
        ULONG shareAccess,
        ULONG openOptions)
    {
        CompiledDefinitionsRedirectHook* const hook = active_;
        if (hook == nullptr || hook->originalNativeOpen_ == nullptr)
        {
            return static_cast<NTSTATUS>(0xC000000DUL);
        }

        OBJECT_ATTRIBUTES redirectedAttributes = {};
        bool redirect = false;
        __try
        {
            if (objectAttributes != nullptr &&
                IsGameDefinitionsPath(objectAttributes->ObjectName))
            {
                redirectedAttributes = *objectAttributes;
                redirect = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            redirect = false;
        }

        UNICODE_STRING redirectedName = {};
        if (redirect)
        {
            const std::size_t pathBytes =
                hook->gameDefinitionsNtPath_.size() * sizeof(wchar_t);
            redirectedName.Buffer = const_cast<PWSTR>(
                hook->gameDefinitionsNtPath_.c_str());
            redirectedName.Length = static_cast<USHORT>(pathBytes);
            redirectedName.MaximumLength = static_cast<USHORT>(
                pathBytes + sizeof(wchar_t));
            redirectedAttributes.RootDirectory = nullptr;
            redirectedAttributes.ObjectName = &redirectedName;
            hook->redirectOccurred_.store(true, std::memory_order_release);
            hook->Report(hook->diagnostics_);
        }

        return hook->originalNativeOpen_(
            fileHandle,
            desiredAccess,
            redirect ? &redirectedAttributes : objectAttributes,
            ioStatusBlock,
            shareAccess,
            openOptions);
    }
}
