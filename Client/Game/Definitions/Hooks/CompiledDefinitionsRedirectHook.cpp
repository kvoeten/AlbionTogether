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

    void WriteRelativeJump(void* source, const void* destination)
    {
        auto* const bytes = static_cast<std::uint8_t*>(source);
        bytes[0] = 0xE9;
        const auto displacement = static_cast<std::int32_t>(
            reinterpret_cast<std::intptr_t>(destination) -
            (reinterpret_cast<std::intptr_t>(source) + 5));
        std::memcpy(bytes + 1, &displacement, sizeof(displacement));
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

        constexpr std::size_t kTrampolineSize =
            native::CreateFileFunctions::PrologueSize + 5;
        constexpr std::size_t kPatchCount = 4;
        void* const trampolineMemory = VirtualAlloc(
            nullptr,
            kTrampolineSize * kPatchCount,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE);
        if (trampolineMemory == nullptr)
        {
            return false;
        }

        auto* const trampolineBytes = static_cast<std::uint8_t*>(
            trampolineMemory);
        std::array<std::uint8_t*, kPatchCount> trampolines = {};
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
        for (std::size_t index = 0; index < kPatchCount; ++index)
        {
            trampolines[index] = trampolineBytes + index * kTrampolineSize;
            std::memcpy(
                trampolines[index],
                targets[index],
                native::CreateFileFunctions::PrologueSize);
            WriteRelativeJump(
                trampolines[index] + native::CreateFileFunctions::PrologueSize,
                static_cast<const std::uint8_t*>(targets[index]) +
                    native::CreateFileFunctions::PrologueSize);
        }
        DWORD discardedProtection = 0;
        if (!VirtualProtect(
                trampolineMemory,
                kTrampolineSize * kPatchCount,
                PAGE_EXECUTE_READ,
                &discardedProtection))
        {
            VirtualFree(trampolineMemory, 0, MEM_RELEASE);
            return false;
        }

        gameDefinitionsPath_ = gameDefinitionsPath;
        gameDefinitionsNtPath_ = ntPath;
        gameDefinitionsPathAnsi_ = std::move(ansiPath);
        originalWide_ = reinterpret_cast<native::CreateFileFunctions::WideFunction>(
            trampolines[0]);
        originalAnsi_ = reinterpret_cast<native::CreateFileFunctions::AnsiFunction>(
            trampolines[1]);
        originalNative_ = reinterpret_cast<native::CreateFileFunctions::NativeFunction>(
            trampolines[2]);
        originalNativeOpen_ =
            reinterpret_cast<native::CreateFileFunctions::NativeOpenFunction>(
                trampolines[3]);
        trampolineMemory_ = trampolineMemory;
        targets_ = targets;
        active_ = this;

        std::array<DWORD, kPatchCount> previousProtections = {};
        std::size_t patchedCount = 0;
        bool patchSucceeded = true;
        for (std::size_t index = 0; index < kPatchCount; ++index)
        {
            if (!VirtualProtect(
                    targets[index],
                    native::CreateFileFunctions::PrologueSize,
                    PAGE_EXECUTE_READWRITE,
                    &previousProtections[index]))
            {
                patchSucceeded = false;
                break;
            }
            WriteRelativeJump(targets[index], replacements[index]);
            patchedCount = index + 1;
            FlushInstructionCache(
                GetCurrentProcess(),
                targets[index],
                native::CreateFileFunctions::PrologueSize);
            DWORD ignoredProtection = 0;
            if (!VirtualProtect(
                    targets[index],
                    native::CreateFileFunctions::PrologueSize,
                    previousProtections[index],
                    &ignoredProtection))
            {
                patchSucceeded = false;
                break;
            }
        }
        if (!patchSucceeded)
        {
            while (patchedCount > 0)
            {
                --patchedCount;
                DWORD writableProtection = 0;
                if (VirtualProtect(
                        targets[patchedCount],
                        native::CreateFileFunctions::PrologueSize,
                        PAGE_EXECUTE_READWRITE,
                        &writableProtection))
                {
                    std::memcpy(
                        targets[patchedCount],
                        trampolines[patchedCount],
                        native::CreateFileFunctions::PrologueSize);
                    FlushInstructionCache(
                        GetCurrentProcess(),
                        targets[patchedCount],
                        native::CreateFileFunctions::PrologueSize);
                    DWORD ignoredProtection = 0;
                    VirtualProtect(
                        targets[patchedCount],
                        native::CreateFileFunctions::PrologueSize,
                        previousProtections[patchedCount],
                        &ignoredProtection);
                }
            }
            active_ = nullptr;
            originalWide_ = nullptr;
            originalAnsi_ = nullptr;
            originalNative_ = nullptr;
            originalNativeOpen_ = nullptr;
            trampolineMemory_ = nullptr;
            targets_ = {};
            VirtualFree(trampolineMemory, 0, MEM_RELEASE);
            return false;
        }
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
        if (installed_ && trampolineMemory_ != nullptr)
        {
            if (targets_[0] != nullptr)
            {
                constexpr std::size_t bytes = native::CreateFileFunctions::PrologueSize;
                constexpr std::size_t trampolineSize = bytes + 5;
                auto* originals = static_cast<std::uint8_t*>(trampolineMemory_);
                for (std::size_t index = targets_.size(); index > 0; --index)
                {
                    void* target = targets_[index - 1];
                    DWORD protection = 0;
                    if (VirtualProtect(target, bytes, PAGE_EXECUTE_READWRITE, &protection))
                    {
                        std::memcpy(
                            target,
                            originals + (index - 1) * trampolineSize,
                            bytes);
                        FlushInstructionCache(GetCurrentProcess(), target, bytes);
                        DWORD discarded = 0;
                        VirtualProtect(target, bytes, protection, &discarded);
                    }
                }
            }
        }
        if (active_ == this) active_ = nullptr;
        originalWide_ = nullptr;
        originalAnsi_ = nullptr;
        originalNative_ = nullptr;
        originalNativeOpen_ = nullptr;
        if (trampolineMemory_ != nullptr) VirtualFree(trampolineMemory_, 0, MEM_RELEASE);
        trampolineMemory_ = nullptr;
        targets_ = {};
        gameDefinitionsPath_.clear();
        gameDefinitionsNtPath_.clear();
        gameDefinitionsPathAnsi_.clear();
        diagnostics_ = {};
        installed_ = false;
    }

    bool CompiledDefinitionsRedirectHook::IsInstalled() const noexcept
    {
        return installed_ && active_ == this &&
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
