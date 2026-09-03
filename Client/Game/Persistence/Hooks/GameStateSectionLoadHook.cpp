#include "GameStateSectionLoadHook.h"

#include "Core/Target/ExecutableValidator.h"
#include "Game/Native/Addresses.h"

#include <array>
#include <cstdint>

namespace
{
    constexpr std::array<std::uint8_t, 14> LoadSectionsPrefix = {
        0x6A, 0xFF, 0x68, 0xF2, 0xB0, 0x95, 0x02,
        0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50};
    constexpr std::uintptr_t LoadSectionsExceptionTargetRva = 0x0255B0F2;

    bool IsExecutableRange(const void* const address) noexcept
    {
        MEMORY_BASIC_INFORMATION memory = {};
        if (address == nullptr ||
            VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
        {
            return false;
        }
        const DWORD protection = memory.Protect & 0xFFu;
        return protection == PAGE_EXECUTE ||
            protection == PAGE_EXECUTE_READ ||
            protection == PAGE_EXECUTE_READWRITE ||
            protection == PAGE_EXECUTE_WRITECOPY;
    }
}

namespace fable::game::persistence
{
    GameStateSectionLoadHook* GameStateSectionLoadHook::active_ = nullptr;

    bool GameStateSectionLoadHook::Install(
        const HMODULE gameModule,
        const CompletionSink sink,
        void* const context,
        const core::Diagnostics& diagnostics) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule;
        (void)sink;
        (void)context;
        diagnostics.Log(
            "Hook: game-state section completion requires the x86 client.");
        return false;
#else
        if (gameModule == nullptr || sink == nullptr || IsInstalled() ||
            (active_ != nullptr && active_ != this))
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const target = reinterpret_cast<void*>(
            base + native::rva::GameStateBundleLoadSections);
        if (!IsExecutableRange(target) ||
            !core::target::ValidateFableExecutable(gameModule, nullptr))
        {
            diagnostics.Event(
                "MultiplayerGameStateSectionLoadHookBlocked",
                "current executable did not expose the validated retail save-bundle loader");
            return false;
        }
        std::array<std::uint8_t, LoadSectionsPrefix.size()> expected =
            LoadSectionsPrefix;
        *reinterpret_cast<std::uint32_t*>(expected.data() + 3) =
            static_cast<std::uint32_t>(
                base + LoadSectionsExceptionTargetRva);
        diagnostics_ = diagnostics;
        sink_ = sink;
        sinkContext_ = context;
        active_ = this;
        if (!detour_.Install(
                target,
                expected.data(),
                expected.size(),
                reinterpret_cast<void*>(&LoadSectionsObserved),
                expected.size()))
        {
            active_ = nullptr;
            sink_ = nullptr;
            sinkContext_ = nullptr;
            diagnostics_ = {};
            return false;
        }
        original_ = reinterpret_cast<LoadSections>(detour_.Original());
        diagnostics_.Event(
            "MultiplayerGameStateSectionLoadHookReady",
            "guest host-owned state is finalized after ENTITIES, PLAYER, QUESTS, REGIONS, and FACTIONS load");
        return true;
#endif
    }

    void GameStateSectionLoadHook::Shutdown() noexcept
    {
        if (!detour_.Shutdown())
        {
            diagnostics_.Log(
                "Hook: game-state section completion shutdown deferred because its target is owned by another hook.");
            return;
        }
        original_ = nullptr;
        sink_ = nullptr;
        sinkContext_ = nullptr;
        diagnostics_ = {};
        if (active_ == this) active_ = nullptr;
    }

    void __fastcall GameStateSectionLoadHook::LoadSectionsObserved(
        void* const bundle,
        void*,
        void* const reader)
    {
        if (active_ != nullptr)
        {
            active_->ObserveLoadSections(bundle, reader);
        }
    }

    void GameStateSectionLoadHook::ObserveLoadSections(
        void* const bundle,
        void* const reader)
    {
        if (original_ == nullptr)
        {
            return;
        }
        original_(bundle, reader);
        if (sink_ != nullptr)
        {
            sink_(sinkContext_);
        }
    }
}
