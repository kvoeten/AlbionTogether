#include "QuestStateNativeCaptureHook.h"

#include "Game/Native/Addresses.h"
#include "Game/Native/ScriptTypes.h"
#include "Core/Target/FableNativeLayout.h"
#include "Core/Target/ExecutableValidator.h"
#include "Multiplayer/Protocol/QuestStateSnapshotMessage.h"

#include <array>
#include <cstdio>
#include <limits>
#include <string>
#include <algorithm>
#include <cstring>

namespace
{
    // The first 14 bytes end after the SEH frame's push eax and contain no
    // relative instruction. The relocated operand is intentionally excluded
    // from this exact prefix check by the executable validator; this hook is
    // additionally protected by that validator's current-build gate.
    constexpr std::array<std::uint8_t, 14> SavePrefix = {
        0x6A, 0xFF, 0x68, 0x30, 0xE3, 0x95, 0x02, 0x64,
        0xA1, 0x00, 0x00, 0x00, 0x00, 0x50};
    constexpr std::array<std::uint8_t, 14> LoadPrefix = {
        0x6A, 0xFF, 0x68, 0x2C, 0xE6, 0x95, 0x02, 0x64,
        0xA1, 0x00, 0x00, 0x00, 0x00, 0x50};
}

namespace fable::multiplayer::persistence
{
    QuestStateNativeCaptureHook* QuestStateNativeCaptureHook::active_ = nullptr;

    void QuestStateNativeCaptureHook::BindGameModule(
        const HMODULE gameModule,
        const core::Diagnostics& diagnostics) noexcept
    {
        gameModule_ = gameModule;
        diagnostics_ = diagnostics;
    }

    bool QuestStateNativeCaptureHook::Install(
        HMODULE gameModule,
        const CaptureSink sink,
        void* const context,
        const core::Diagnostics& diagnostics) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule;
        (void)sink;
        (void)context;
        diagnostics.Log("Hook: quest save capture requires the x86 client.");
        return false;
#else
        if (gameModule == nullptr || sink == nullptr || IsInstalled() ||
            (active_ != nullptr && active_ != this))
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* target = reinterpret_cast<void*>(
            base + game::native::rva::QuestManagerSaveGameState);
        std::array<std::uint8_t, SavePrefix.size()> expected = SavePrefix;
        *reinterpret_cast<std::uint32_t*>(expected.data() + 3) =
            static_cast<std::uint32_t>(base + 0x0255E330);
        if (!target ||
            !core::target::ValidateFableExecutable(gameModule, nullptr) ||
            *reinterpret_cast<std::uint8_t*>(target) != SavePrefix[0])
        {
            diagnostics.Log(
                "Hook: quest save capture refused because the current PE build was not validated.");
            return false;
        }
        diagnostics_ = diagnostics;
        gameModule_ = gameModule;
        sink_ = sink;
        sinkContext_ = context;
        active_ = this;
        if (!detour_.Install(
                target,
                expected.data(),
                SavePrefix.size(),
                reinterpret_cast<void*>(&SaveGameStateObserved),
                SavePrefix.size()))
        {
            active_ = nullptr;
            gameModule_ = nullptr;
            sink_ = nullptr;
            sinkContext_ = nullptr;
            return false;
        }
        original_ = reinterpret_cast<SaveGameState>(detour_.Original());
        diagnostics_.Event(
            "MultiplayerQuestStateNativeCaptureReady",
            "retail CQuestManager::SaveGameState output is observed after native serialization");
        return true;
#endif
    }

    bool QuestStateNativeCaptureHook::InstallLoadOverride(
        HMODULE gameModule,
        const SnapshotProvider provider,
        const ApplyResultSink resultSink,
        void* const context,
        const core::Diagnostics& diagnostics) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule;
        (void)provider;
        (void)resultSink;
        (void)context;
        diagnostics.Log("Hook: quest load override requires the x86 client.");
        return false;
#else
        if (gameModule == nullptr || provider == nullptr ||
            IsLoadOverrideInstalled() ||
            (active_ != nullptr && active_ != this))
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const target = reinterpret_cast<void*>(
            base + game::native::rva::QuestManagerLoadGameState);
        std::array<std::uint8_t, LoadPrefix.size()> expected = LoadPrefix;
        *reinterpret_cast<std::uint32_t*>(expected.data() + 3) =
            static_cast<std::uint32_t>(base + 0x0255E62C);
        if (!core::target::ValidateFableExecutable(gameModule, nullptr))
        {
            diagnostics.Log(
                "Hook: quest load override refused because the current PE build was not validated.");
            return false;
        }

        diagnostics_ = diagnostics;
        gameModule_ = gameModule;
        snapshotProvider_ = provider;
        applyResultSink_ = resultSink;
        loadContext_ = context;
        active_ = this;
        if (!loadDetour_.Install(
                target,
                expected.data(),
                expected.size(),
                reinterpret_cast<void*>(&LoadGameStateObserved),
                expected.size()))
        {
            active_ = nullptr;
            snapshotProvider_ = nullptr;
            applyResultSink_ = nullptr;
            loadContext_ = nullptr;
            gameModule_ = nullptr;
            diagnostics_ = {};
            return false;
        }
        originalLoad_ = reinterpret_cast<LoadGameState>(
            loadDetour_.Original());
        diagnostics_.Event(
            "MultiplayerQuestStateNativeLoadOverrideReady",
            "guest QUESTS parser input is replaced at the native CQuestManager load boundary");
        return true;
#endif
    }

    void QuestStateNativeCaptureHook::Shutdown() noexcept
    {
        const bool loadStopped = loadDetour_.Shutdown();
        const bool captureStopped = detour_.Shutdown();
        if (!loadStopped || !captureStopped)
        {
            diagnostics_.Log(
                "Hook: quest persistence shutdown deferred because a target is owned by another hook.");
            return;
        }
        original_ = nullptr;
        originalLoad_ = nullptr;
        sink_ = nullptr;
        sinkContext_ = nullptr;
        snapshotProvider_ = nullptr;
        applyResultSink_ = nullptr;
        loadContext_ = nullptr;
        gameModule_ = nullptr;
        diagnostics_ = {};
        if (active_ == this) active_ = nullptr;
    }

    bool QuestStateNativeCaptureHook::CaptureCurrent()
    {
#if !defined(_M_IX86)
        return false;
#else
        if (original_ == nullptr || gameModule_ == nullptr || sink_ == nullptr)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        void* manager = nullptr;
        __try
        {
            manager = *reinterpret_cast<void**>(
                base + game::native::rva::QuestManagerGlobal);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            manager = nullptr;
        }
        if (manager == nullptr)
        {
            diagnostics_.Event(
                "MultiplayerQuestStateNativeCaptureBlocked",
                "CQuestManager global was not live for the initial host capture");
            return false;
        }
        using CharStringConstructor = void(__thiscall*)(
            game::native::CharString*, const char*, int);
        using CharStringDestructor = void(__thiscall*)(
            game::native::CharString*);
        const auto constructString = reinterpret_cast<CharStringConstructor>(
            base + game::native::rva::CharStringConstructor);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + game::native::rva::CharStringDestructor);
        game::native::CharString output = {};
        bool constructed = false;
        bool captured = false;
        // The __finally preserves native ownership cleanup while allowing any
        // retail exception from SaveGameState to propagate to its caller.
        __try
        {
            constructString(&output, "", -1);
            constructed = true;
            original_(manager, &output);
            captured = CaptureOutput(output);
        }
        __finally
        {
            if (constructed) destroyString(&output);
        }
        return captured;
#endif
    }

    bool QuestStateNativeCaptureHook::CaptureOutput(
        const game::native::CharString& output) noexcept
    {
        if (sink_ == nullptr) return false;
        const auto sink = sink_;
        void* const context = sinkContext_;
        __try
        {
            const auto* const rep = static_cast<
                const game::native::StringRep*>(output.stringData);
            if (rep == nullptr || rep->len < 0 ||
                static_cast<unsigned long long>(rep->len) >
                    protocol::MaximumQuestStateSnapshotBytes ||
                (rep->len != 0 && rep->data == nullptr))
            {
                return false;
            }
            sink(context,
                reinterpret_cast<const std::uint8_t*>(rep->data),
                static_cast<std::size_t>(rep->len));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            diagnostics_.Event(
                "MultiplayerQuestStateNativeCaptureBlocked",
                "native CCharString representation was unreadable after SaveGameState");
            return false;
        }
    }

    bool QuestStateNativeCaptureHook::ApplySnapshot(
        const std::uint8_t* const bytes,
        const std::size_t byteCount) noexcept
    {
#if !defined(_M_IX86)
        (void)bytes;
        (void)byteCount;
        diagnostics_.Event(
            "MultiplayerQuestStateSnapshotApplyBlocked",
            "CStringParser native apply is only available in the x86 client");
        return false;
#else
        if ((byteCount != 0 && bytes == nullptr) ||
            byteCount > protocol::MaximumQuestStateSnapshotBytes ||
            (bytes != nullptr && std::find(bytes, bytes + byteCount, 0) !=
                bytes + byteCount))
        {
            diagnostics_.Event(
                "MultiplayerQuestStateSnapshotApplyBlocked",
                "host quest text contains an embedded NUL or exceeds the native bound");
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        if (base == 0)
        {
            return false;
        }
        void* manager = nullptr;
        __try
        {
            manager = *reinterpret_cast<void**>(
                base + game::native::rva::QuestManagerGlobal);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            manager = nullptr;
        }
        if (manager == nullptr)
        {
            diagnostics_.Event(
                "MultiplayerQuestStateSnapshotApplyBlocked",
                "CQuestManager global was not live at the post-section boundary");
            return false;
        }
        const auto loadGameState = originalLoad_ != nullptr
            ? originalLoad_
            : reinterpret_cast<LoadGameState>(
                base + game::native::rva::QuestManagerLoadGameState);
        return ApplySnapshotToManager(
            manager, loadGameState, bytes, byteCount);
#endif
    }

    bool QuestStateNativeCaptureHook::ApplySnapshotToManager(
        void* const manager,
        const LoadGameState loadGameState,
        const std::uint8_t* const bytes,
        const std::size_t byteCount) noexcept
    {
#if !defined(_M_IX86)
        (void)manager;
        (void)loadGameState;
        (void)bytes;
        (void)byteCount;
        return false;
#else
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule_);
        if (manager == nullptr || loadGameState == nullptr || base == 0 ||
            (byteCount != 0 && bytes == nullptr) ||
            byteCount > protocol::MaximumQuestStateSnapshotBytes ||
            (bytes != nullptr && std::find(bytes, bytes + byteCount, 0) !=
                bytes + byteCount))
        {
            return false;
        }
        using CharStringConstructor = void(__thiscall*)(
            game::native::CharString*, const char*, int);
        using CharStringDestructor = void(__thiscall*)(
            game::native::CharString*);
        using ParserConstructor = void(__thiscall*)(
            void*, const game::native::CharString*, void*,
            const game::native::CharString*, int);
        using ParserDestructor = void(__thiscall*)(void*);
        const auto constructString = reinterpret_cast<CharStringConstructor>(
            base + game::native::rva::CharStringConstructor);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + game::native::rva::CharStringDestructor);
        const auto constructParser = reinterpret_cast<ParserConstructor>(
            base + game::native::rva::CStringParserConstructor);
        const auto destroyParser = reinterpret_cast<ParserDestructor>(
            base + game::native::rva::CStringParserDestructor);
        if (byteCount == (std::numeric_limits<std::size_t>::max)())
        {
            return false;
        }
        auto* const text = static_cast<char*>(HeapAlloc(
            GetProcessHeap(), 0, byteCount + 1));
        if (text == nullptr)
        {
            return false;
        }
        if (byteCount != 0) std::memcpy(text, bytes, byteCount);
        text[byteCount] = '\0';
        game::native::CharString source = {};
        game::native::CharString empty = {};
        alignas(4) std::array<std::uint8_t, 0x1C> parser = {};
        bool sourceConstructed = false;
        bool emptyConstructed = false;
        bool parserConstructed = false;
        bool loaded = false;
        __try
        {
            // CPersist's validated helper passes length -1, callback 0,
            // an empty CCharString context, and mode 0.
            constructString(&source, text, -1);
            sourceConstructed = true;
            constructString(&empty, "", -1);
            emptyConstructed = true;
            constructParser(parser.data(), &source, nullptr, &empty, 0);
            parserConstructed = true;
            loadGameState(manager, parser.data());
            loaded = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            loaded = false;
        }
        if (parserConstructed)
        {
            __try { destroyParser(parser.data()); }
            __except (EXCEPTION_EXECUTE_HANDLER) { loaded = false; }
        }
        if (emptyConstructed)
        {
            __try { destroyString(&empty); }
            __except (EXCEPTION_EXECUTE_HANDLER) { loaded = false; }
        }
        if (sourceConstructed)
        {
            __try { destroyString(&source); }
            __except (EXCEPTION_EXECUTE_HANDLER) { loaded = false; }
        }
        HeapFree(GetProcessHeap(), 0, text);
        if (!loaded)
        {
            diagnostics_.Event(
                "MultiplayerQuestStateSnapshotApplyBlocked",
                "native CStringParser/CQuestManager load raised or rejected the staged snapshot");
        }
        return loaded;
#endif
    }

    void __fastcall QuestStateNativeCaptureHook::LoadGameStateObserved(
        void* const manager,
        void*,
        void* const parser)
    {
        if (active_ != nullptr)
        {
            active_->ObserveLoadGameState(manager, parser);
        }
    }

    void QuestStateNativeCaptureHook::ObserveLoadGameState(
        void* const manager,
        void* const parser)
    {
        if (originalLoad_ == nullptr)
        {
            return;
        }
        const std::uint8_t* bytes = nullptr;
        std::size_t byteCount = 0;
        const bool hasOverride = snapshotProvider_ != nullptr &&
            snapshotProvider_(loadContext_, bytes, byteCount);
        if (!hasOverride)
        {
            originalLoad_(manager, parser);
            return;
        }
        const bool applied = ApplySnapshotToManager(
            manager, originalLoad_, bytes, byteCount);
        if (applyResultSink_ != nullptr)
        {
            applyResultSink_(loadContext_, applied);
        }
    }

    void __fastcall QuestStateNativeCaptureHook::SaveGameStateObserved(
        void* const manager,
        void*,
        game::native::CharString* const output)
    {
        if (active_ != nullptr)
        {
            active_->ObserveSaveGameState(manager, output);
        }
    }

    void QuestStateNativeCaptureHook::ObserveSaveGameState(
        void* const manager,
        game::native::CharString* const output)
    {
        if (original_ == nullptr)
        {
            return;
        }
        // Native code is allowed to raise while constructing or populating a
        // CCharString. Let retail complete first, then inspect only the
        // documented {data,len} representation under an SEH guard.
        if (output == nullptr) return;
        original_(manager, output);
        (void)CaptureOutput(*output);
    }
}
