#include "GuildTeleportSafetyHook.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{
    constexpr std::uintptr_t TeleportHeroToGuildRva = 0x01A22DE0;
    constexpr std::uintptr_t CurrentInventoryEntryRva = 0x01A22E27;
    constexpr std::uintptr_t CurrentInventoryEntryResumeRva = 0x01A22E2E;
    constexpr std::uintptr_t MissingInventoryEntryResumeRva = 0x01A22E38;
    constexpr std::uintptr_t ForwardInventoryRotationRva = 0x018BA26E;
    constexpr std::uintptr_t ForwardInventoryRotationResumeRva = 0x018BA275;
    constexpr std::uintptr_t MissingForwardInventoryResumeRva = 0x018BA279;
    constexpr std::uintptr_t BackwardInventoryRotationRva = 0x018BA2BC;
    constexpr std::uintptr_t BackwardInventoryRotationResumeRva = 0x018BA2C3;
    constexpr std::uintptr_t MissingBackwardInventoryResumeRva = 0x018BA2C7;

    constexpr std::array<std::uint8_t, 16> TeleportHeroToGuildPrefix = {
        0x56, 0x8B, 0xF1, 0x8B, 0x86, 0x84, 0x01, 0x00,
        0x00, 0x8B, 0x50, 0x0C, 0x57, 0x8D, 0x8E, 0x84
    };
    constexpr std::array<std::uint8_t, 7> CurrentInventoryEntryInstructions = {
        0x8B, 0x10, 0x8B, 0xC8, 0x8B, 0x42, 0x08
    };
    constexpr std::array<std::uint8_t, 10> CurrentInventoryEntryResume = {
        0xFF, 0xD0, 0x8B, 0x16, 0x89, 0x86, 0xD0, 0x02, 0x00, 0x00
    };
    constexpr std::array<std::uint8_t, 6> MissingInventoryEntryResume = {
        0x8B, 0x82, 0xC0, 0x00, 0x00, 0x00
    };
    constexpr std::array<std::uint8_t, 7> InventoryRotationInstructions = {
        0x8B, 0x10, 0x8B, 0xC8, 0xFF, 0x52, 0x0C
    };
    constexpr std::array<std::uint8_t, 4> ForwardInventoryRotationResume = {
        0x84, 0xC0, 0x74, 0xD5
    };
    constexpr std::array<std::uint8_t, 5> MissingForwardInventoryResume = {
        0x47, 0x3B, 0x7C, 0x24, 0x10
    };
    constexpr std::array<std::uint8_t, 4> BackwardInventoryRotationResume = {
        0x84, 0xC0, 0x74, 0xD4
    };
    constexpr std::array<std::uint8_t, 5> MissingBackwardInventoryResume = {
        0x47, 0x3B, 0x7C, 0x24, 0x0C
    };

    std::uintptr_t g_currentInventoryEntryResume = 0;
    std::uintptr_t g_missingInventoryEntryResume = 0;
    std::uintptr_t g_forwardInventoryRotationResume = 0;
    std::uintptr_t g_missingForwardInventoryResume = 0;
    std::uintptr_t g_backwardInventoryRotationResume = 0;
    std::uintptr_t g_missingBackwardInventoryResume = 0;

    bool IsRangeInsideImage(
        HMODULE gameModule,
        std::uintptr_t rva,
        std::size_t size) noexcept
    {
        if (gameModule == nullptr || size == 0)
        {
            return false;
        }

        const auto* const base = reinterpret_cast<const std::uint8_t*>(gameModule);
        const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
        {
            return false;
        }
        const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
        {
            return false;
        }
        const std::size_t imageSize = nt->OptionalHeader.SizeOfImage;
        return rva < imageSize && size <= imageSize - rva;
    }

    template <std::size_t Size>
    bool Matches(
        HMODULE gameModule,
        std::uintptr_t rva,
        const std::array<std::uint8_t, Size>& expected) noexcept
    {
        if (!IsRangeInsideImage(gameModule, rva, expected.size()))
        {
            return false;
        }
        const auto* const address =
            reinterpret_cast<const std::uint8_t*>(gameModule) + rva;
        return std::memcmp(address, expected.data(), expected.size()) == 0;
    }

#if defined(_M_IX86)
    void __stdcall LogGuildTeleportFallback(unsigned int kind);

    __declspec(naked) void GuardGuildTeleportInventoryEntry()
    {
        __asm
        {
            test eax, eax
            jz missing_entry

            // Re-run the displaced retail instructions, then call the
            // selected map-inventory entry through its native virtual.
            mov edx, dword ptr [eax]
            mov ecx, eax
            mov eax, dword ptr [edx + 8]
            jmp dword ptr [g_currentInventoryEntryResume]

        missing_entry:
            // Fable's own map-inventory lookup falls back to slot zero when
            // the selected slot is absent. Preserve that convention instead
            // of leaving -1 for the later teleport-completion restore.
            pushfd
            pushad
            push 0
            call LogGuildTeleportFallback
            popad
            popfd
            xor eax, eax
            mov dword ptr [esi + 2D0h], eax
            mov edx, dword ptr [esi]
            jmp dword ptr [g_missingInventoryEntryResume]
        }
    }

    __declspec(naked) void GuardForwardInventoryRotation()
    {
        __asm
        {
            test eax, eax
            jz missing_entry

            mov edx, dword ptr [eax]
            mov ecx, eax
            call dword ptr [edx + 0Ch]
            jmp dword ptr [g_forwardInventoryRotationResume]

        missing_entry:
            pushfd
            pushad
            push 1
            call LogGuildTeleportFallback
            popad
            popfd
            // A null result means this inventory category has no candidate.
            // Complete the category instead of spinning its one-entry ring.
            jmp dword ptr [g_missingForwardInventoryResume]
        }
    }

    __declspec(naked) void GuardBackwardInventoryRotation()
    {
        __asm
        {
            test eax, eax
            jz missing_entry

            mov edx, dword ptr [eax]
            mov ecx, eax
            call dword ptr [edx + 0Ch]
            jmp dword ptr [g_backwardInventoryRotationResume]

        missing_entry:
            pushfd
            pushad
            push 2
            call LogGuildTeleportFallback
            popad
            popfd
            // As above, there is no object whose validity virtual can be
            // queried, so this category is already exhausted.
            jmp dword ptr [g_missingBackwardInventoryResume]
        }
    }
#endif
}

namespace fable::game::hero_pawn::travel::hooks
{
    struct GuildTeleportSafetyThunkAccess final
    {
        static void LogFallback(unsigned int kind) noexcept
        {
            if (GuildTeleportSafetyHook::active_ != nullptr)
            {
                GuildTeleportSafetyHook::active_->LogFallback(kind);
            }
        }
    };

    GuildTeleportSafetyHook* GuildTeleportSafetyHook::active_ = nullptr;

    bool GuildTeleportSafetyHook::Install(
        HMODULE gameModule,
        const core::Diagnostics& diagnostics)
    {
        if (IsInstalled())
        {
            return true;
        }
        if (active_ == this || installed_ || currentEntry_.IsInstalled() ||
            forwardRotation_.IsInstalled() || backwardRotation_.IsInstalled())
        {
            diagnostics.Log(
                "Hook: Guild teleport safety installation is partially active; shutdown is required before retrying.");
            return false;
        }
        if (active_ != nullptr && active_ != this)
        {
            diagnostics.Log(
                "Hook: another Guild teleport safety hook is already active.");
            return false;
        }

#if !defined(_M_IX86)
        diagnostics.Log(
            "Hook: Guild teleport safety is only supported by the x86 client.");
        return false;
#else
        if (!Matches(
                gameModule,
                TeleportHeroToGuildRva,
                TeleportHeroToGuildPrefix) ||
            !Matches(
                gameModule,
                CurrentInventoryEntryRva,
                CurrentInventoryEntryInstructions) ||
            !Matches(
                gameModule,
                CurrentInventoryEntryResumeRva,
                CurrentInventoryEntryResume) ||
            !Matches(
                gameModule,
                MissingInventoryEntryResumeRva,
                MissingInventoryEntryResume) ||
            !Matches(
                gameModule,
                ForwardInventoryRotationRva,
                InventoryRotationInstructions) ||
            !Matches(
                gameModule,
                ForwardInventoryRotationResumeRva,
                ForwardInventoryRotationResume) ||
            !Matches(
                gameModule,
                MissingForwardInventoryResumeRva,
                MissingForwardInventoryResume) ||
            !Matches(
                gameModule,
                BackwardInventoryRotationRva,
                InventoryRotationInstructions) ||
            !Matches(
                gameModule,
                BackwardInventoryRotationResumeRva,
                BackwardInventoryRotationResume) ||
            !Matches(
                gameModule,
                MissingBackwardInventoryResumeRva,
                MissingBackwardInventoryResume))
        {
            diagnostics.Log(
                "Hook: Guild teleport safety target failed current-build ABI validation.");
            return false;
        }

        auto* const base = reinterpret_cast<std::uint8_t*>(gameModule);
        auto* const currentEntry = base + CurrentInventoryEntryRva;
        auto* const forwardRotation = base + ForwardInventoryRotationRva;
        auto* const backwardRotation = base + BackwardInventoryRotationRva;
        diagnostics_ = diagnostics;
        g_currentInventoryEntryResume =
            reinterpret_cast<std::uintptr_t>(base + CurrentInventoryEntryResumeRva);
        g_missingInventoryEntryResume =
            reinterpret_cast<std::uintptr_t>(base + MissingInventoryEntryResumeRva);
        g_forwardInventoryRotationResume = reinterpret_cast<std::uintptr_t>(
            base + ForwardInventoryRotationResumeRva);
        g_missingForwardInventoryResume = reinterpret_cast<std::uintptr_t>(
            base + MissingForwardInventoryResumeRva);
        g_backwardInventoryRotationResume = reinterpret_cast<std::uintptr_t>(
            base + BackwardInventoryRotationResumeRva);
        g_missingBackwardInventoryResume = reinterpret_cast<std::uintptr_t>(
            base + MissingBackwardInventoryResumeRva);
        active_ = this;
        installed_ = true;

        auto installPatch = [this](
            std::uint8_t* target,
            const auto& expected,
            void* replacement,
            core::hooking::CodePatch& site,
            const char* name) noexcept
        {
            if (!site.InstallRelativeJump(
                    target,
                    expected.data(),
                    expected.size(),
                    replacement,
                    expected.size()))
            {
                diagnostics_.Log(name);
                return false;
            }
            return true;
        };

        if (!installPatch(
                currentEntry,
                CurrentInventoryEntryInstructions,
                reinterpret_cast<void*>(&GuardGuildTeleportInventoryEntry),
                currentEntry_,
                "Hook: Guild teleport current-entry guard could not be installed.") ||
            !installPatch(
                forwardRotation,
                InventoryRotationInstructions,
                reinterpret_cast<void*>(&GuardForwardInventoryRotation),
                forwardRotation_,
                "Hook: Guild teleport forward-rotation guard could not be installed.") ||
            !installPatch(
                backwardRotation,
                InventoryRotationInstructions,
                reinterpret_cast<void*>(&GuardBackwardInventoryRotation),
                backwardRotation_,
                "Hook: Guild teleport backward-rotation guard could not be installed."))
        {
            Shutdown();
            return false;
        }

        diagnostics_.Log(
            "Hook: Guild teleport map-inventory safety guards installed.");
        diagnostics_.Event(
            "GuildTeleportSafetyReady",
            "fallback_map_inventory_index=0");
        return true;
#endif
    }

    void GuildTeleportSafetyHook::Shutdown() noexcept
    {
        const bool backwardRestored = backwardRotation_.Shutdown();
        const bool forwardRestored = forwardRotation_.Shutdown();
        const bool currentRestored = currentEntry_.Shutdown();
        if (!backwardRestored || !forwardRestored || !currentRestored)
        {
            diagnostics_.Event(
                "GuildTeleportHookUninstallSkipped",
                "target-changed-by-another-hook");
            return;
        }
        if (backwardRotation_.ProtectionRestoreFailed() ||
            forwardRotation_.ProtectionRestoreFailed() ||
            currentEntry_.ProtectionRestoreFailed())
        {
            diagnostics_.Event(
                "GuildTeleportHookProtectionRestoreWarning",
                "original-bytes-restored");
        }

        if (active_ == this)
        {
            active_ = nullptr;
        }
        fallbackEventsLogged_.store(0, std::memory_order_relaxed);
        installed_ = false;
        g_currentInventoryEntryResume = 0;
        g_missingInventoryEntryResume = 0;
        g_forwardInventoryRotationResume = 0;
        g_missingForwardInventoryResume = 0;
        g_backwardInventoryRotationResume = 0;
        g_missingBackwardInventoryResume = 0;
        diagnostics_ = {};
    }

    bool GuildTeleportSafetyHook::IsInstalled() const noexcept
    {
        return installed_ && active_ == this && currentEntry_.IsInstalled() &&
            forwardRotation_.IsInstalled() && backwardRotation_.IsInstalled();
    }

    void GuildTeleportSafetyHook::LogFallback(unsigned int kind) noexcept
    {
        const unsigned int ordinal = fallbackEventsLogged_.fetch_add(
            1, std::memory_order_relaxed);
        if (ordinal >= 8)
        {
            return;
        }

        const char* detail = "stage=current-entry fallback=slot-zero";
        if (kind == 1)
        {
            detail = "stage=forward-rotation fallback=skip-empty-category";
        }
        else if (kind == 2)
        {
            detail = "stage=backward-rotation fallback=skip-empty-category";
        }
        diagnostics_.Event("GuildTeleportInventoryFallback", detail);
    }
}

#if defined(_M_IX86)
namespace
{
    void __stdcall LogGuildTeleportFallback(unsigned int kind)
    {
        fable::game::hero_pawn::travel::hooks::
            GuildTeleportSafetyThunkAccess::LogFallback(kind);
    }
}
#endif
