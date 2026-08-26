#include "CodePatch.h"

#include <climits>
#include <cstring>
#include <utility>

namespace
{
    enum class WriteResult : std::uint8_t
    {
        Failed,
        Succeeded,
        SucceededProtectionRestoreFailed,
    };

    bool ReadableBytesEqual(
        const void* address,
        const std::vector<std::uint8_t>& expected) noexcept
    {
        if (address == nullptr || expected.empty())
        {
            return false;
        }
        __try
        {
            return std::memcmp(
                       address,
                       expected.data(),
                       expected.size()) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool CopyBytes(
        void* destination,
        const void* source,
        std::size_t size) noexcept
    {
        if (destination == nullptr || source == nullptr || size == 0)
        {
            return false;
        }
        __try
        {
            std::memcpy(destination, source, size);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    WriteResult WriteBytes(
        void* target,
        const std::uint8_t* bytes,
        std::size_t size) noexcept
    {
        if (target == nullptr || bytes == nullptr || size == 0)
        {
            return WriteResult::Failed;
        }
        DWORD previousProtection = 0;
        if (!VirtualProtect(
                target,
                size,
                PAGE_EXECUTE_READWRITE,
                &previousProtection))
        {
            return WriteResult::Failed;
        }

        if (!CopyBytes(target, bytes, size))
        {
            DWORD discardedProtection = 0;
            (void)VirtualProtect(
                target,
                size,
                previousProtection,
                &discardedProtection);
            return WriteResult::Failed;
        }
        FlushInstructionCache(GetCurrentProcess(), target, size);
        DWORD discardedProtection = 0;
        const BOOL protectionRestored = VirtualProtect(
            target,
            size,
            previousProtection,
            &discardedProtection);
        return protectionRestored != FALSE
            ? WriteResult::Succeeded
            : WriteResult::SucceededProtectionRestoreFailed;
    }

    bool BuildRelativeJump(
        const std::uint8_t* source,
        const void* destination,
        std::uint8_t* output) noexcept
    {
        if (source == nullptr || destination == nullptr || output == nullptr)
        {
            return false;
        }
        const std::intptr_t displacement =
            reinterpret_cast<std::intptr_t>(destination) -
            (reinterpret_cast<std::intptr_t>(source) + 5);
        if (displacement < INT32_MIN || displacement > INT32_MAX)
        {
            return false;
        }
        output[0] = 0xE9;
        const auto relative = static_cast<std::int32_t>(displacement);
        std::memcpy(output + 1, &relative, sizeof(relative));
        return true;
    }
}

namespace fable::core::hooking
{
    CodePatch::~CodePatch() noexcept
    {
        (void)Shutdown();
    }

    bool CodePatch::Install(
        void* target,
        const void* expected,
        const std::size_t expectedSize,
        const void* replacement,
        const std::size_t replacementSize) noexcept
    {
        if (target == nullptr || expected == nullptr || replacement == nullptr ||
            expectedSize == 0 || replacementSize == 0 ||
            expectedSize > replacementSize || IsInstalled())
        {
            return false;
        }

        try
        {
            std::vector<std::uint8_t> expectedBytes(expectedSize);
            std::vector<std::uint8_t> originalBytes(replacementSize);
            std::vector<std::uint8_t> replacementBytes(replacementSize);
            if (!CopyBytes(expectedBytes.data(), expected, expectedSize) ||
                !CopyBytes(
                    originalBytes.data(), target, replacementSize) ||
                !CopyBytes(
                    replacementBytes.data(), replacement, replacementSize))
            {
                return false;
            }
            if (!ReadableBytesEqual(target, expectedBytes))
            {
                return false;
            }
            const WriteResult writeResult = WriteBytes(
                target,
                replacementBytes.data(),
                replacementSize);
            if (writeResult == WriteResult::Failed)
            {
                return false;
            }
            target_ = target;
            original_ = std::move(originalBytes);
            replacement_ = std::move(replacementBytes);
            uninstallWasSkipped_ = false;
            protectionRestoreFailed_ =
                writeResult == WriteResult::SucceededProtectionRestoreFailed;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool CodePatch::InstallRelativeJump(
        void* target,
        const void* expected,
        const std::size_t expectedSize,
        void* replacement,
        const std::size_t displacedBytes) noexcept
    {
#if !defined(_M_IX86)
        (void)target;
        (void)expected;
        (void)expectedSize;
        (void)replacement;
        (void)displacedBytes;
        return false;
#else
        if (target == nullptr || expected == nullptr || replacement == nullptr ||
            expectedSize == 0 || expectedSize > displacedBytes ||
            displacedBytes < 5 || IsInstalled())
        {
            return false;
        }
        try
        {
            std::vector<std::uint8_t> jump(displacedBytes, 0x90);
            if (!BuildRelativeJump(
                    static_cast<std::uint8_t*>(target),
                    replacement,
                    jump.data()))
            {
                return false;
            }
            return Install(
                target,
                expected,
                expectedSize,
                jump.data(),
                jump.size());
        }
        catch (...)
        {
            return false;
        }
#endif
    }

    bool CodePatch::Shutdown() noexcept
    {
        if (target_ == nullptr)
        {
            return true;
        }

        bool bytesRestored = false;
        if (ReadableBytesEqual(target_, replacement_))
        {
            const WriteResult writeResult = WriteBytes(
                target_,
                original_.data(),
                original_.size());
            bytesRestored = writeResult != WriteResult::Failed;
            protectionRestoreFailed_ =
                writeResult == WriteResult::SucceededProtectionRestoreFailed;
        }
        else
        {
            uninstallWasSkipped_ = true;
            return false;
        }
        if (!bytesRestored)
        {
            return false;
        }
        target_ = nullptr;
        original_.clear();
        replacement_.clear();
        // Keep this diagnostic bit available after ownership is cleared so a
        // caller can report the protection warning without retaining a stale
        // installed state or leaking an otherwise unreachable trampoline.
        return true;
    }

    InlineHook::~InlineHook() noexcept
    {
        (void)Shutdown();
    }

    bool InlineHook::Install(
        void* target,
        const void* expected,
        const std::size_t expectedSize,
        void* replacement,
        const std::size_t displacedBytes) noexcept
    {
#if !defined(_M_IX86)
        (void)target;
        (void)expected;
        (void)expectedSize;
        (void)replacement;
        (void)displacedBytes;
        return false;
#else
        if (target == nullptr || expected == nullptr || replacement == nullptr ||
            expectedSize == 0 || expectedSize > displacedBytes ||
            displacedBytes < 5 || IsInstalled())
        {
            return false;
        }

        std::uint8_t* trampoline = nullptr;
        try
        {
            trampoline = static_cast<std::uint8_t*>(VirtualAlloc(
                nullptr,
                displacedBytes + 5,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_EXECUTE_READWRITE));
            if (trampoline == nullptr)
            {
                return false;
            }

            if (!CopyBytes(trampoline, target, displacedBytes))
            {
                VirtualFree(trampoline, 0, MEM_RELEASE);
                trampoline = nullptr;
                return false;
            }
            if (!BuildRelativeJump(
                    trampoline + displacedBytes,
                    static_cast<std::uint8_t*>(target) + displacedBytes,
                    trampoline + displacedBytes))
            {
                VirtualFree(trampoline, 0, MEM_RELEASE);
                trampoline = nullptr;
                return false;
            }

            DWORD previousProtection = 0;
            if (!VirtualProtect(
                    trampoline,
                    displacedBytes + 5,
                    PAGE_EXECUTE_READ,
                    &previousProtection))
            {
                VirtualFree(trampoline, 0, MEM_RELEASE);
                return false;
            }
            FlushInstructionCache(
                GetCurrentProcess(), trampoline, displacedBytes + 5);

            if (!patch_.InstallRelativeJump(
                    target,
                    expected,
                    expectedSize,
                    replacement,
                    displacedBytes))
            {
                VirtualFree(trampoline, 0, MEM_RELEASE);
                trampoline = nullptr;
                return false;
            }
            trampoline_ = trampoline;
            trampoline = nullptr;
            return true;
        }
        catch (...)
        {
            if (trampoline != nullptr)
            {
                VirtualFree(trampoline, 0, MEM_RELEASE);
            }
            return false;
        }
#endif
    }

    bool InlineHook::Shutdown() noexcept
    {
        const bool restored = patch_.Shutdown();
        if (!restored)
        {
            // The target may now be owned by another hook. Keep the
            // trampoline executable until the patch can be removed safely.
            return false;
        }
        if (trampoline_ != nullptr)
        {
            VirtualFree(trampoline_, 0, MEM_RELEASE);
            trampoline_ = nullptr;
        }
        return restored;
    }
}
