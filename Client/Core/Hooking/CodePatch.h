#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fable::core::hooking
{
    // Owns one in-process code or vtable replacement. Install and Shutdown
    // must run while every thread that can execute the target is quiescent.
    // This primitive deliberately does not suspend threads or provide an
    // in-flight call barrier; callers must establish that lifecycle invariant
    // before releasing a patch or its trampoline.
    class CodePatch final
    {
    public:
        CodePatch() noexcept = default;
        ~CodePatch() noexcept;

        CodePatch(const CodePatch&) = delete;
        CodePatch& operator=(const CodePatch&) = delete;

        [[nodiscard]] bool Install(
            void* target,
            const void* expected,
            std::size_t expectedSize,
            const void* replacement,
            std::size_t replacementSize) noexcept;

        // Installs an x86 relative jump padded with NOPs. This is the shared
        // path for naked/mid-function hooks that already own their resume
        // addresses and therefore do not need an allocated trampoline.
        [[nodiscard]] bool InstallRelativeJump(
            void* target,
            const void* expected,
            std::size_t expectedSize,
            void* replacement,
            std::size_t displacedBytes) noexcept;

        // Returns false when the original bytes could not be restored. In
        // particular, a changed target is left untouched so a later hook is
        // never silently overwritten; state and executable ownership are
        // retained so a caller can retry after the competing hook is removed.
        // A true result means the bytes are restored and ownership is cleared;
        // ProtectionRestoreFailed() separately reports a page-protection
        // restoration warning.
        [[nodiscard]] bool Shutdown() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept
        {
            return target_ != nullptr;
        }

        [[nodiscard]] bool UninstallWasSkipped() const noexcept
        {
            return uninstallWasSkipped_;
        }

        [[nodiscard]] bool ProtectionRestoreFailed() const noexcept
        {
            return protectionRestoreFailed_;
        }

    private:
        void* target_ = nullptr;
        std::vector<std::uint8_t> original_;
        std::vector<std::uint8_t> replacement_;
        bool uninstallWasSkipped_ = false;
        bool protectionRestoreFailed_ = false;
    };

    // Builds a five-byte x86 relative-jump trampoline and owns the target
    // patch plus executable trampoline memory. The same quiescence contract
    // as CodePatch applies to Shutdown and destruction. If Shutdown returns
    // false because another hook owns the target, the trampoline is retained;
    // the caller must keep this object alive and retry, or intentionally leave
    // it allocated until process teardown.
    class InlineHook final
    {
    public:
        InlineHook() noexcept = default;
        ~InlineHook() noexcept;

        InlineHook(const InlineHook&) = delete;
        InlineHook& operator=(const InlineHook&) = delete;

        [[nodiscard]] bool Install(
            void* target,
            const void* expected,
            std::size_t expectedSize,
            void* replacement,
            std::size_t displacedBytes) noexcept;
        [[nodiscard]] bool Shutdown() noexcept;

        [[nodiscard]] void* Original() const noexcept
        {
            return trampoline_;
        }

        [[nodiscard]] bool IsInstalled() const noexcept
        {
            return patch_.IsInstalled() && trampoline_ != nullptr;
        }

        [[nodiscard]] bool UninstallWasSkipped() const noexcept
        {
            return patch_.UninstallWasSkipped();
        }

        [[nodiscard]] bool ProtectionRestoreFailed() const noexcept
        {
            return patch_.ProtectionRestoreFailed();
        }

    private:
        CodePatch patch_;
        void* trampoline_ = nullptr;
    };
}
