#include "FrontEndAutomationScenario.h"
#include "Core/Bootstrap/ClientRuntimeServices.h"
#include "Core/Bootstrap/FeatureRegistry.h"
#include "Core/Target/FableNativeLayout.h"
#include "Automation/CharacterSnapshot/CharacterSnapshotScenario.h"
#include "UI/FrontEnd/Hooks/FrontEndLifecycleHooks.h"
#include "UI/FrontEnd/Hooks/FrontEndStartInitializerHook.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>

namespace fable::automation::front_end
{
using namespace fable::core::bootstrap;
using namespace fable::core::target;
using namespace fable::automation::character_snapshot;

    // Front-end automation is scoped to the game window. Posting messages to
    // this HWND does not alter the user's active application or require focus.
    constexpr ULONGLONG kSyntheticEnterReleaseDelayMs = 150;

    bool PostSyntheticEnterDown() noexcept
    {
        const HWND window = UiContext().gameWindow;
        if (window == nullptr || !IsWindow(window))
        {
            return false;
        }
        const UINT scanCode = MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC);
        const LPARAM keyDown = 1 | (static_cast<LPARAM>(scanCode) << 16);
        if (!PostMessageW(window, WM_KEYDOWN, VK_RETURN, keyDown))
        {
            return false;
        }
        const ULONGLONG now = GetTickCount64();
        FrontEndContext().passStartSubmittedAt.store(now, std::memory_order_release);
        FrontEndContext().passStartKeyDownAt.store(now, std::memory_order_release);
        LogEvent("FrontEndInputPosted", "game window WM_KEYDOWN VK_RETURN");
        return true;
    }

    bool PostSyntheticEnterUp() noexcept
    {
        const HWND window = UiContext().gameWindow;
        if (window == nullptr || !IsWindow(window))
        {
            return false;
        }
        const UINT scanCode = MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC);
        const LPARAM keyUp = 1 |
            (static_cast<LPARAM>(scanCode) << 16) |
            (static_cast<LPARAM>(1) << 30) |
            (static_cast<LPARAM>(1) << 31);
        if (!PostMessageW(window, WM_KEYUP, VK_RETURN, keyUp))
        {
            return false;
        }
        FrontEndContext().passStartKeyDownAt.store(0, std::memory_order_release);
        LogEvent("FrontEndInputPosted", "game window WM_KEYUP VK_RETURN");
        return true;
    }

    void LogUiLifecycleEvent(
        const char* state,
        const void* object,
        const void* frame,
        std::size_t virtualMethodIndex)
    {
        const unsigned int observation =
            FrontEndContext().uiLifecycleEventsLogged.fetch_add(1, std::memory_order_relaxed) + 1;
        if (observation > 256)
        {
            return;
        }

        void* vtable = nullptr;
        void* implementation = nullptr;
        __try
        {
            if (object != nullptr)
            {
                vtable = *reinterpret_cast<void* const*>(object);
                if (vtable != nullptr && virtualMethodIndex != 0)
                {
                    implementation = reinterpret_cast<void* const*>(vtable)[virtualMethodIndex];
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            vtable = nullptr;
            implementation = nullptr;
        }

        LogFormat(
            "Lifecycle: state=%s event=%u thread=%lu object=%p frame=%p vtable=%p method_index=%zu implementation=%p.",
            state,
            observation,
            static_cast<unsigned long>(GetCurrentThreadId()),
            object,
            frame,
            vtable,
            virtualMethodIndex,
            implementation);

        char detail[512] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "event=%u object=%p frame=%p vtable=%p method_index=%zu implementation=%p",
            observation,
            object,
            frame,
            vtable,
            virtualMethodIndex,
            implementation);
        LogEvent(state, detail);

        const auto expectedFrontEndDoBegin = reinterpret_cast<void*>(
            reinterpret_cast<std::uintptr_t>(CoreContext().gameModule) + kFrontEndMainMenuDoBeginRva);
        const auto expectedLoadGameDoBegin = reinterpret_cast<void*>(
            reinterpret_cast<std::uintptr_t>(CoreContext().gameModule) + kLoadGamePageDoBeginRva);
        if (std::strcmp(state, "UiPageDoBegin") == 0 &&
            implementation == expectedFrontEndDoBegin &&
            !FrontEndContext().readyLogged.exchange(true, std::memory_order_acq_rel))
        {
            FrontEndContext().mainMenuObject.store(
                const_cast<void*>(object),
                std::memory_order_release);
            FrontEndContext().readyAt.store(GetTickCount64(), std::memory_order_release);
            LogFormat(
                "Lifecycle: Fable front-end main menu is ready; object=%p vtable=%p implementation=%p.",
                object,
                vtable,
                implementation);
            LogEvent("FrontendReady", detail);
            __try
            {
                const auto* const bytes = static_cast<const std::uint8_t*>(object);
                char stateDetail[320] = {};
                std::snprintf(
                    stateDetail,
                    std::size(stateDetail),
                    "object=%p flags=0x%08lX phase=%u field74=0x%08lX field78=0x%08lX field7C=0x%08lX field84=0x%08lX",
                    object,
                    static_cast<unsigned long>(
                        *reinterpret_cast<const std::uint32_t*>(bytes + 0x58)),
                    static_cast<unsigned int>(*(bytes + 0x68)),
                    static_cast<unsigned long>(
                        *reinterpret_cast<const std::uint32_t*>(bytes + 0x74)),
                    static_cast<unsigned long>(
                        *reinterpret_cast<const std::uint32_t*>(bytes + 0x78)),
                    static_cast<unsigned long>(
                        *reinterpret_cast<const std::uint32_t*>(bytes + 0x7C)),
                    static_cast<unsigned long>(
                        *reinterpret_cast<const std::uint32_t*>(bytes + 0x84)));
                LogEvent("FrontEndMainMenuState", stateDetail);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                LogEvent("ClientFailed", "front-end-main-menu-state-read-fault");
            }
        }
        else if (std::strcmp(state, "UiPageDoBegin") == 0 &&
            implementation == expectedLoadGameDoBegin &&
            !FrontEndContext().saveListRequested.exchange(true, std::memory_order_acq_rel))
        {
            FrontEndContext().loadGamePageObject.store(
                const_cast<void*>(object),
                std::memory_order_release);
            LogFormat(
                "Lifecycle: Load Game page began; object=%p vtable=%p implementation=%p. No save will be selected.",
                object,
                vtable,
                implementation);
            LogEvent("SaveListRequested", detail);
        }
    }

    void __stdcall ObserveUiPageDoBegin(void* object, const void* frame)
    {
        LogUiLifecycleEvent("UiPageDoBegin", object, frame, 78);
    }

    void __stdcall ObserveUiPageDoInit(void* object, const void* frame)
    {
        __try
        {
            const void* const expectedVtable = reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(CoreContext().gameModule) + kLoadGamePageVtableRva);
            if (object != nullptr && *reinterpret_cast<void* const*>(object) == expectedVtable)
            {
                FrontEndContext().loadGamePageObject.store(
                    const_cast<void*>(object),
                    std::memory_order_release);
                LogEvent("SaveListObjectReady", "captured UI_PageLoadGame object during DoInit");
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        LogUiLifecycleEvent("UiPageDoInit", object, frame, 77);
    }

    void __stdcall ObserveUiPageStartPlay(void* object, const void* frame)
    {
        LogUiLifecycleEvent("UiPageStartPlay", object, frame, 86);
    }

    void __stdcall ObservePlayLoadMapMovie(void* object, const void* frame)
    {
        LogUiLifecycleEvent("PlayLoadMapMovie", object, frame, 0);
        char detail[160] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "object=%p frame=%p thread=%lu",
            object,
            frame,
            static_cast<unsigned long>(GetCurrentThreadId()));
        LogEvent("MapLoadStarted", detail);
    }

    void __stdcall ObserveFrontEndStartDoInit(void* object, const void* frame)
    {
        ULONGLONG expectedReadyAt = 0;
        FrontEndContext().startReadyAt.compare_exchange_strong(
            expectedReadyAt,
            GetTickCount64(),
            std::memory_order_acq_rel,
            std::memory_order_acquire);
        for (auto& candidate : FrontEndContext().startObjects)
        {
            void* expected = nullptr;
            if (candidate.compare_exchange_strong(
                    expected,
                    object,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire) || expected == object)
            {
                break;
            }
        }
        LogUiLifecycleEvent("FrontEndStartDoInit", object, frame, 0);
        LogEvent("FrontEndStartReady", "captured UI_MenuFrontEndStart object");
    }

    void TryPassFrontEndStart(void* object, const void* frame, const char* source)
    {
        if (object == nullptr || !ScenarioUsesFrontEndStartAutomation())
        {
            return;
        }

        const unsigned int inputStage =
            FrontEndContext().passStartStage.load(std::memory_order_acquire);
        const ULONGLONG now = GetTickCount64();
        if (inputStage == 2)
        {
            const ULONGLONG submittedAt =
                FrontEndContext().passStartSubmittedAt.load(std::memory_order_acquire);
            if (!FrontEndContext().readyLogged.load(std::memory_order_acquire) &&
                now - submittedAt >= 2'000 &&
                FrontEndContext().passStartAttempts.load(std::memory_order_acquire) < 5)
            {
                FrontEndContext().passStartStage.store(0, std::memory_order_release);
                LogEvent("FrontEndInputRetry", "title screen still active");
            }
            return;
        }
        if (inputStage == 3)
        {
            const ULONGLONG armedAt =
                FrontEndContext().passStartSubmittedAt.load(std::memory_order_acquire);
            if (armedAt != 0 && now - armedAt >= kSyntheticEnterReleaseDelayMs)
            {
                if (PostSyntheticEnterUp())
                {
                    FrontEndContext().passStartSubmittedAt.store(
                        now,
                        std::memory_order_release);
                    FrontEndContext().passStartStage.store(2, std::memory_order_release);
                }
            }
            if (armedAt != 0 && now - armedAt >= 500)
            {
                if (FrontEndContext().passStartAttempts.load(std::memory_order_acquire) >= 5)
                {
                    FrontEndContext().passStartStage.store(4, std::memory_order_release);
                    LogEvent("ClientFailed", "front-end-input-attempt-limit-reached");
                }
                else
                {
                    FrontEndContext().passStartStage.store(0, std::memory_order_release);
                    LogEvent("FrontEndInputRetry", "game-window Enter message was not accepted");
                }
            }
            return;
        }
        if (inputStage != 0)
        {
            return;
        }

        const ULONGLONG readyAt =
            FrontEndContext().startReadyAt.load(std::memory_order_acquire);
        if (readyAt == 0 || now - readyAt < 3'000)
        {
            return;
        }

        std::uint32_t stateFlags = 0;
        __try
        {
            stateFlags = *reinterpret_cast<const std::uint32_t*>(
                static_cast<const std::uint8_t*>(object) + 0x40);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return;
        }
        if ((stateFlags & 8u) == 0)
        {
            return;
        }

        unsigned int expectedStage = 0;
        if (!FrontEndContext().passStartStage.compare_exchange_strong(
                expectedStage,
                3,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            return;
        }
        const unsigned int attempt =
            FrontEndContext().passStartAttempts.fetch_add(1, std::memory_order_acq_rel) + 1;

        LogUiLifecycleEvent("FrontEndStartAutomationTick", object, frame, 0);
        if (!PostSyntheticEnterDown())
        {
            FrontEndContext().passStartStage.store(0, std::memory_order_release);
            LogEvent("FrontEndInputRetry", "game-window Enter keydown post failed");
            return;
        }

        LogFormat(
            "Automation: posting game-window Enter attempt %u from %s; object=%p state_flags=0x%08lX thread=%lu.",
            attempt,
            source,
            object,
            static_cast<unsigned long>(stateFlags),
            static_cast<unsigned long>(GetCurrentThreadId()));

        FrontEndContext().passStartStage.store(3, std::memory_order_release);
        LogEvent("FrontEndInputArmed", "game window WM_KEYDOWN/WM_KEYUP Enter");
    }

    void DriveBootstrapFixtureProbe()
    {
        if (!ScenarioIs(L"bootstrap_fixture_probe") ||
            !FrontEndContext().readyLogged.load(std::memory_order_acquire) ||
            FrontEndContext().startInvoked.load(std::memory_order_acquire))
        {
            return;
        }

        void* const object =
            FrontEndContext().mainMenuObject.load(std::memory_order_acquire);
        if (object == nullptr)
        {
            return;
        }

        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(object);
            void* const vtable = *reinterpret_cast<void* const*>(object);
            void* const expectedVtable = reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(CoreContext().gameModule) +
                kFrontEndMainMenuVtableRva);
            if (vtable != expectedVtable)
            {
                LogEvent("ClientFailed", "bootstrap-main-menu-vtable-validation-failed");
                FrontEndContext().startInvoked.store(true, std::memory_order_release);
                return;
            }

            const std::uint32_t flags =
                *reinterpret_cast<const std::uint32_t*>(bytes + 0x58);
            const std::uint8_t phase = *(bytes + 0x68);
            if ((flags & 4u) == 0)
            {
                const ULONGLONG now = GetTickCount64();
                ULONGLONG previousTick =
                    FrontEndContext().mainMenuLastTickAt.load(std::memory_order_acquire);
                if (now - previousTick < 16 ||
                    !FrontEndContext().mainMenuLastTickAt.compare_exchange_strong(
                        previousTick,
                        now,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire))
                {
                    return;
                }

                void* const implementation =
                    reinterpret_cast<void* const*>(vtable)[80];
                void* const expectedImplementation = reinterpret_cast<void*>(
                    reinterpret_cast<std::uintptr_t>(CoreContext().gameModule) +
                    kFrontEndMainMenuDoTickRva);
                if (implementation != expectedImplementation)
                {
                    LogEvent("ClientFailed", "bootstrap-main-menu-DoTick-validation-failed");
                    FrontEndContext().startInvoked.store(true, std::memory_order_release);
                    return;
                }

                using MainMenuDoTick = void(__thiscall*)(void*, float);
                reinterpret_cast<MainMenuDoTick>(implementation)(object, 1.0f / 60.0f);
                const std::uint32_t updatedFlags =
                    *reinterpret_cast<const std::uint32_t*>(bytes + 0x58);
                const std::uint8_t updatedPhase = *(bytes + 0x68);
                const unsigned int tick =
                    FrontEndContext().mainMenuTickCount.fetch_add(
                        1,
                        std::memory_order_acq_rel) + 1;
                const unsigned int previousPhase =
                    FrontEndContext().mainMenuLastLoggedPhase.exchange(
                        updatedPhase,
                        std::memory_order_acq_rel);
                if (tick <= 3 || previousPhase != updatedPhase ||
                    (updatedFlags & 4u) != 0)
                {
                    char detail[256] = {};
                    std::snprintf(
                        detail,
                        std::size(detail),
                        "tick=%u object=%p flags=0x%08lX phase=%u field7C=0x%08lX",
                        tick,
                        object,
                        static_cast<unsigned long>(updatedFlags),
                        static_cast<unsigned int>(updatedPhase),
                        static_cast<unsigned long>(
                            *reinterpret_cast<const std::uint32_t*>(bytes + 0x7C)));
                    LogEvent("BootstrapMainMenuTickProgress", detail);
                }
                return;
            }

            if (!FrontEndContext().mainMenuReady.exchange(true, std::memory_order_acq_rel))
            {
                char detail[256] = {};
                std::snprintf(
                    detail,
                    std::size(detail),
                    "object=%p flags=0x%08lX phase=%u field74=0x%08lX field78=0x%08lX field7C=0x%08lX",
                    object,
                    static_cast<unsigned long>(flags),
                    static_cast<unsigned int>(phase),
                    static_cast<unsigned long>(
                        *reinterpret_cast<const std::uint32_t*>(bytes + 0x74)),
                    static_cast<unsigned long>(
                        *reinterpret_cast<const std::uint32_t*>(bytes + 0x78)),
                    static_cast<unsigned long>(
                        *reinterpret_cast<const std::uint32_t*>(bytes + 0x7C)));
                LogEvent("BootstrapMainMenuReady", detail);
                return;
            }

            void* const implementation =
                reinterpret_cast<void* const*>(vtable)[81];
            void* const expectedImplementation = reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(CoreContext().gameModule) +
                kFrontEndMainMenuDoOnUIEventRva);
            if (implementation != expectedImplementation)
            {
                LogEvent("ClientFailed", "bootstrap-main-menu-OnUIEvent-validation-failed");
                FrontEndContext().startInvoked.store(true, std::memory_order_release);
                return;
            }

            FrontEndContext().startInvoked.store(true, std::memory_order_release);
            FrontEndContext().startInvokedAt.store(GetTickCount64(), std::memory_order_release);
            using MainMenuDoOnUIEvent = void(__thiscall*)(void*, int, int);
            LogEvent(
                "NewGameStartInvoked",
                "calling validated main-menu OnUIEvent(17, 0) in the isolated fixture profile");
            reinterpret_cast<MainMenuDoOnUIEvent>(implementation)(object, 17, 0);
            LogEvent(
                "NewGameStartRequested",
                "validated main-menu OnUIEvent returned in the isolated fixture profile");
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            FrontEndContext().startInvoked.store(true, std::memory_order_release);
            LogEvent("ClientFailed", "bootstrap-main-menu-native-call-raised-structured-exception");
        }
    }

    void DriveSaveListObservation()
    {
        if ((!ScenarioIs(L"observe_save_list") && !ScenarioLoadsFixture()) ||
            !FrontEndContext().readyLogged.load(std::memory_order_acquire) ||
            FrontEndContext().saveListRequested.load(std::memory_order_acquire))
        {
            return;
        }

        const ULONGLONG readyAt = FrontEndContext().readyAt.load(std::memory_order_acquire);
        if (readyAt == 0 || GetTickCount64() - readyAt < 1'500)
        {
            return;
        }
        if (FrontEndContext().saveListBeginAttempted.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }

        void* const object = FrontEndContext().loadGamePageObject.load(std::memory_order_acquire);
        if (object == nullptr)
        {
            LogEvent("ClientFailed", "load-game-page-object-unavailable");
            return;
        }

        __try
        {
            void* const expectedVtable = reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(CoreContext().gameModule) + kLoadGamePageVtableRva);
            void* const expectedDoBegin = reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(CoreContext().gameModule) + kLoadGamePageDoBeginRva);
            void* const vtable = *reinterpret_cast<void* const*>(object);
            void* const implementation = vtable == nullptr
                ? nullptr
                : reinterpret_cast<void* const*>(vtable)[78];
            void* const parent = *reinterpret_cast<void* const*>(
                static_cast<const std::uint8_t*>(object) + 0x50);
            if (vtable != expectedVtable ||
                implementation != expectedDoBegin ||
                parent == nullptr)
            {
                char detail[256] = {};
                std::snprintf(
                    detail,
                    std::size(detail),
                    "object=%p vtable=%p expected_vtable=%p implementation=%p expected_implementation=%p parent=%p",
                    object,
                    vtable,
                    expectedVtable,
                    implementation,
                    expectedDoBegin,
                    parent);
                LogEvent("ClientFailed", detail);
                return;
            }

            using LoadGamePageDoBegin = void(__thiscall*)(void*);
            LogEvent(
                "SaveListBeginInvoked",
                "calling validated UI_PageLoadGame::DoBegin on the game thread; StartPlay is not called");
            reinterpret_cast<LoadGamePageDoBegin>(implementation)(object);
            FrontEndContext().saveListRequested.store(true, std::memory_order_release);
            LogEvent(
                "SaveListRequested",
                "validated UI_PageLoadGame::DoBegin returned; no save was selected");
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            LogEvent("ClientFailed", "load-game-page-DoBegin-raised-structured-exception");
        }
    }

    bool ReadSaveEntryName(
        const std::uint8_t* entry,
        wchar_t (&name)[261],
        std::uint32_t& length)
    {
        const auto* const storage = *reinterpret_cast<const std::uint8_t* const*>(
            entry + 0x04);
        if (storage == nullptr)
        {
            return false;
        }

        length = *reinterpret_cast<const std::uint32_t*>(storage + 0x10);
        const std::uint32_t capacity =
            *reinterpret_cast<const std::uint32_t*>(storage + 0x14);
        if (length > 260 || capacity > 4'096 || length > capacity)
        {
            return false;
        }

        const wchar_t* const characters = capacity >= 8
            ? *reinterpret_cast<const wchar_t* const*>(storage)
            : reinterpret_cast<const wchar_t*>(storage);
        if (characters == nullptr)
        {
            return false;
        }

        std::wmemcpy(name, characters, length);
        name[length] = L'\0';
        return true;
    }

    bool ObserveLocalSaveEntries(
        const void* loadGamePage,
        const wchar_t* exactSaveName,
        std::uint32_t* exactSaveIdentity)
    {
        __try
        {
            const auto* const page = static_cast<const std::uint8_t*>(loadGamePage);
            const auto* const parent = *reinterpret_cast<const std::uint8_t* const*>(
                page + 0x50);
            if (parent == nullptr)
            {
                LogEvent("SaveEntryEnumerationUnavailable", "load-game parent is null");
                return false;
            }

            const std::uint32_t parentFlags =
                *reinterpret_cast<const std::uint32_t*>(parent + 0x60);
            if ((parentFlags & 1u) != 0)
            {
                char detail[128] = {};
                std::snprintf(
                    detail,
                    std::size(detail),
                    "provider=alternate parent_flags=0x%08lX",
                    static_cast<unsigned long>(parentFlags));
                LogEvent("SaveEntryEnumerationUnavailable", detail);
                return false;
            }

            const auto base = reinterpret_cast<std::uintptr_t>(CoreContext().gameModule);
            const auto* const manager = *reinterpret_cast<const std::uint8_t* const*>(
                base + kLocalSaveManagerSlotRva);
            if (manager == nullptr)
            {
                LogEvent("SaveEntryEnumerationUnavailable", "local save manager is null");
                return false;
            }

            const auto* const begin = *reinterpret_cast<const std::uint8_t* const*>(
                manager + 0x104);
            const auto* const end = *reinterpret_cast<const std::uint8_t* const*>(
                manager + 0x108);
            const auto beginAddress = reinterpret_cast<std::uintptr_t>(begin);
            const auto endAddress = reinterpret_cast<std::uintptr_t>(end);
            constexpr std::size_t kEntryStride = 0x2C;
            if (begin == nullptr || endAddress < beginAddress ||
                (endAddress - beginAddress) % kEntryStride != 0 ||
                (endAddress - beginAddress) / kEntryStride > 64)
            {
                char detail[192] = {};
                std::snprintf(
                    detail,
                    std::size(detail),
                    "provider=local parent_flags=0x%08lX manager=%p begin=%p end=%p invalid-range=true",
                    static_cast<unsigned long>(parentFlags),
                    manager,
                    begin,
                    end);
                LogEvent("SaveEntryEnumerationUnavailable", detail);
                return false;
            }

            const std::size_t count =
                (endAddress - beginAddress) / kEntryStride;
            char summary[192] = {};
            std::snprintf(
                summary,
                std::size(summary),
                "provider=local parent_flags=0x%08lX manager=%p begin=%p end=%p count=%zu",
                static_cast<unsigned long>(parentFlags),
                manager,
                begin,
                end,
                count);
            LogEvent("SaveEntriesReady", summary);

            unsigned int exactSaveMatches = 0;
            std::uint32_t matchedSaveIdentity = 0xFFFFFFFFu;
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto* const entry = begin + index * kEntryStride;
                const std::uint32_t identity =
                    *reinterpret_cast<const std::uint32_t*>(entry);
                const bool valid =
                    *reinterpret_cast<void* const*>(entry + 0x0C) != nullptr;
                wchar_t name[261] = {};
                std::uint32_t nameLength = 0;
                const bool nameReadable =
                    ReadSaveEntryName(entry, name, nameLength);
                if (exactSaveName != nullptr && valid && nameReadable &&
                    std::wcscmp(name, exactSaveName) == 0)
                {
                    ++exactSaveMatches;
                    matchedSaveIdentity = identity;
                }
                char utf8Name[1'024] = "<unreadable>";
                if (nameReadable)
                {
                    const int converted = WideCharToMultiByte(
                        CP_UTF8,
                        0,
                        name,
                        -1,
                        utf8Name,
                        static_cast<int>(std::size(utf8Name)),
                        nullptr,
                        nullptr);
                    if (converted <= 0)
                    {
                        strcpy_s(utf8Name, "<conversion-failed>");
                    }
                }
                char detail[640] = {};
                std::snprintf(
                    detail,
                    std::size(detail),
                    "index=%zu identity=%lu valid=%s name_length=%lu name=%s",
                    index,
                    static_cast<unsigned long>(identity),
                    valid ? "true" : "false",
                    static_cast<unsigned long>(nameLength),
                    utf8Name);
                LogEvent("SaveEntryObserved", detail);
            }

            if (exactSaveIdentity != nullptr)
            {
                if (exactSaveName == nullptr || exactSaveMatches != 1)
                {
                    char utf8Name[256] = "<none>";
                    if (exactSaveName != nullptr)
                    {
                        const int converted = WideCharToMultiByte(
                            CP_UTF8,
                            0,
                            exactSaveName,
                            -1,
                            utf8Name,
                            static_cast<int>(std::size(utf8Name)),
                            nullptr,
                            nullptr);
                        if (converted <= 0)
                        {
                            strcpy_s(utf8Name, "<conversion-failed>");
                        }
                    }
                    char detail[256] = {};
                    std::snprintf(
                        detail,
                        std::size(detail),
                        "exact_name=%s valid_matches=%u",
                        utf8Name,
                        exactSaveMatches);
                    LogEvent("SaveEntrySelectionRejected", detail);
                    return false;
                }
                *exactSaveIdentity = matchedSaveIdentity;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            LogEvent("SaveEntryEnumerationUnavailable", "structured exception while reading local save entries");
            return false;
        }
    }

    void ObserveSaveListReadiness()
    {
        if (!FrontEndContext().saveListRequested.load(std::memory_order_acquire) ||
            FrontEndContext().saveListReadyLogged.load(std::memory_order_acquire))
        {
            return;
        }

        void* const object = FrontEndContext().loadGamePageObject.load(std::memory_order_acquire);
        if (object == nullptr)
        {
            return;
        }

        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(object);
            const ULONGLONG now = GetTickCount64();
            ULONGLONG previousTick =
                FrontEndContext().saveListLastTickAt.load(std::memory_order_acquire);
            if (now - previousTick < 16 ||
                !FrontEndContext().saveListLastTickAt.compare_exchange_strong(
                    previousTick,
                    now,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                return;
            }

            void* const vtable = *reinterpret_cast<void* const*>(object);
            void* const implementation = vtable == nullptr
                ? nullptr
                : reinterpret_cast<void* const*>(vtable)[80];
            void* const expectedImplementation = reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(CoreContext().gameModule) + kLoadGamePageDoTickRva);
            if (implementation != expectedImplementation)
            {
                LogEvent("ClientFailed", "load-game-page-DoTick-validation-failed");
                return;
            }

            using LoadGamePageDoTick = void(__thiscall*)(void*, float);
            reinterpret_cast<LoadGamePageDoTick>(implementation)(object, 1.0f / 60.0f);

            const std::uint32_t flags =
                *reinterpret_cast<const std::uint32_t*>(bytes + 0x58);
            const std::uint8_t phase = *(bytes + 0x68);
            const std::uint32_t selection =
                *reinterpret_cast<const std::uint32_t*>(bytes + 0x74);
            const void* const uiHandle =
                *reinterpret_cast<void* const*>(bytes + 0x7C);
            const unsigned int tick =
                FrontEndContext().saveListTickCount.fetch_add(1, std::memory_order_acq_rel) + 1;
            const unsigned int previousPhase =
                FrontEndContext().saveListLastLoggedPhase.exchange(phase, std::memory_order_acq_rel);
            if (tick <= 3 || previousPhase != phase)
            {
                char progress[256] = {};
                std::snprintf(
                    progress,
                    std::size(progress),
                    "tick=%u object=%p flags=0x%08lX phase=%u selection=%lu ui_handle=%p",
                    tick,
                    object,
                    static_cast<unsigned long>(flags),
                    static_cast<unsigned int>(phase),
                    static_cast<unsigned long>(selection),
                    uiHandle);
                LogEvent("SaveListTickProgress", progress);
            }
            if ((flags & 4u) != 0 &&
                !FrontEndContext().saveListReadyLogged.exchange(true, std::memory_order_acq_rel))
            {
                char detail[256] = {};
                std::snprintf(
                    detail,
                    std::size(detail),
                    "object=%p flags=0x%08lX phase=%u selection=%lu ui_handle=%p no-save-selected=true",
                    object,
                    static_cast<unsigned long>(flags),
                    static_cast<unsigned int>(phase),
                    static_cast<unsigned long>(selection),
                    uiHandle);
                LogFormat("Lifecycle: Load Game save list is ready; %s.", detail);
                LogEvent("SaveListReady", detail);
                const wchar_t* const fixtureSaveName =
                    CoreContext().configuration.FixtureSaveName().c_str();
                std::uint32_t fixtureSaveIdentity = 0xFFFFFFFFu;
                if (!ObserveLocalSaveEntries(
                        object,
                        ScenarioLoadsFixture() ? fixtureSaveName : nullptr,
                        ScenarioLoadsFixture() ? &fixtureSaveIdentity : nullptr))
                {
                    if (ScenarioLoadsFixture())
                    {
                        LogEvent("ClientFailed", "fixture save identity could not be resolved exactly");
                    }
                    return;
                }
                if (ScenarioLoadsFixture())
                {
                    *reinterpret_cast<std::uint32_t*>(
                        static_cast<std::uint8_t*>(object) + 0x74) = fixtureSaveIdentity;
                    FrontEndContext().fixtureSaveIdentity.store(
                        fixtureSaveIdentity,
                        std::memory_order_release);
                    FrontEndContext().fixtureSaveSelectedAt.store(
                        GetTickCount64(),
                        std::memory_order_release);
                    FrontEndContext().fixtureSaveSelected.store(
                        true,
                        std::memory_order_release);
                    char utf8Name[256] = "<conversion-failed>";
                    const int converted = WideCharToMultiByte(
                        CP_UTF8,
                        0,
                        fixtureSaveName,
                        -1,
                        utf8Name,
                        static_cast<int>(std::size(utf8Name)),
                        nullptr,
                        nullptr);
                    if (converted <= 0)
                    {
                        strcpy_s(utf8Name, "<conversion-failed>");
                    }
                    char selected[256] = {};
                    std::snprintf(
                        selected,
                        std::size(selected),
                        "exact_name=%s identity=%lu selection_field=%lu",
                        utf8Name,
                        static_cast<unsigned long>(fixtureSaveIdentity),
                        static_cast<unsigned long>(
                            *reinterpret_cast<const std::uint32_t*>(bytes + 0x74)));
                    LogEvent("FixtureSaveSelected", selected);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            LogEvent("ClientFailed", "save-list-readiness-read-fault");
        }
    }

    void DriveFixtureLoad()
    {
        if (!ScenarioLoadsFixture() ||
            !FrontEndContext().fixtureSaveSelected.load(std::memory_order_acquire) ||
            CharacterSnapshotContext().heroReadyLogged.load(std::memory_order_acquire) ||
            FrontEndContext().mainMenuReleased.load(std::memory_order_acquire))
        {
            return;
        }

        const ULONGLONG selectedAt =
            FrontEndContext().fixtureSaveSelectedAt.load(std::memory_order_acquire);
        if (selectedAt == 0 || GetTickCount64() - selectedAt < 250)
        {
            return;
        }

        void* const object = FrontEndContext().mainMenuObject.load(std::memory_order_acquire);
        if (object == nullptr)
        {
            if (FrontEndContext().fixtureStartInvoked.load(std::memory_order_acquire))
            {
                FrontEndContext().mainMenuReleased.store(true, std::memory_order_release);
                LogEvent(
                    "FixtureMainMenuReleased",
                    "front-end main-menu object cleared after Continue transition");
                return;
            }
            LogEvent("ClientFailed", "front-end main menu disappeared before fixture load");
            FrontEndContext().fixtureStartInvoked.store(true, std::memory_order_release);
            return;
        }

        __try
        {
            auto* const bytes = static_cast<std::uint8_t*>(object);
            void* const vtable = *reinterpret_cast<void* const*>(object);
            void* const expectedVtable = reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(CoreContext().gameModule) +
                kFrontEndMainMenuVtableRva);
            if (vtable != expectedVtable)
            {
                if (FrontEndContext().fixtureStartInvoked.load(std::memory_order_acquire))
                {
                    FrontEndContext().mainMenuReleased.store(
                        true,
                        std::memory_order_release);
                    LogEvent(
                        "FixtureMainMenuReleased",
                        "front-end main-menu object retired after Continue transition");
                    return;
                }
                LogEvent("ClientFailed", "fixture-load main-menu vtable validation failed");
                FrontEndContext().fixtureStartInvoked.store(true, std::memory_order_release);
                return;
            }

            const std::uint32_t expectedIdentity =
                FrontEndContext().fixtureSaveIdentity.load(std::memory_order_acquire);
            const std::uint32_t flags =
                *reinterpret_cast<const std::uint32_t*>(bytes + 0x58);
            const ULONGLONG now = GetTickCount64();

            ULONGLONG previousTick =
                FrontEndContext().mainMenuLastTickAt.load(std::memory_order_acquire);
            if (now - previousTick < 16 ||
                !FrontEndContext().mainMenuLastTickAt.compare_exchange_strong(
                    previousTick,
                    now,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                return;
            }

            if (!FrontEndContext().fixtureStartInvoked.load(std::memory_order_acquire) &&
                (flags & 4u) != 0)
            {
                const std::uint32_t menuState =
                    *reinterpret_cast<const std::uint32_t*>(bytes + 0x7C);
                if ((menuState & 1u) != 0)
                {
                    LogEvent(
                        "ClientFailed",
                        "main menu reports New Game instead of a valid Continue target");
                    FrontEndContext().fixtureStartInvoked.store(true, std::memory_order_release);
                    return;
                }

                void* const implementation =
                    reinterpret_cast<void* const*>(vtable)[81];
                void* const expectedImplementation = reinterpret_cast<void*>(
                    reinterpret_cast<std::uintptr_t>(CoreContext().gameModule) +
                    kFrontEndMainMenuDoOnUIEventRva);
                if (implementation != expectedImplementation)
                {
                    LogEvent(
                        "ClientFailed",
                        "fixture-load main-menu OnUIEvent validation failed");
                    FrontEndContext().fixtureStartInvoked.store(true, std::memory_order_release);
                    return;
                }

                const std::uint32_t previousIdentity =
                    *reinterpret_cast<const std::uint32_t*>(bytes + 0x78);
                *reinterpret_cast<std::uint32_t*>(bytes + 0x78) =
                    expectedIdentity;
                FrontEndContext().fixtureStartInvoked.store(true, std::memory_order_release);
                FrontEndContext().fixtureStartInvokedAt.store(now, std::memory_order_release);
                char detail[256] = {};
                std::snprintf(
                    detail,
                    std::size(detail),
                    "selected_save_identity=%lu previous_continue_identity=%lu menu_state=0x%08lX implementation=%p",
                    static_cast<unsigned long>(expectedIdentity),
                    static_cast<unsigned long>(previousIdentity),
                    static_cast<unsigned long>(menuState),
                    implementation);
                LogEvent("FixtureContinueInvoked", detail);
                using MainMenuDoOnUIEvent = void(__thiscall*)(void*, int, int);
                reinterpret_cast<MainMenuDoOnUIEvent>(implementation)(object, 17, 0);
                LogEvent(
                    "FixtureContinueRequested",
                    "validated main-menu Continue event returned for exact fixture save identity");
                return;
            }

            void* const tickImplementation =
                reinterpret_cast<void* const*>(vtable)[80];
            void* const expectedTickImplementation = reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(CoreContext().gameModule) +
                kFrontEndMainMenuDoTickRva);
            if (tickImplementation != expectedTickImplementation)
            {
                LogEvent("ClientFailed", "fixture-load main-menu DoTick validation failed");
                FrontEndContext().fixtureStartInvoked.store(true, std::memory_order_release);
                return;
            }

            using MainMenuDoTick = void(__thiscall*)(void*, float);
            reinterpret_cast<MainMenuDoTick>(tickImplementation)(
                object,
                1.0f / 60.0f);
            const std::uint32_t updatedFlags =
                *reinterpret_cast<const std::uint32_t*>(bytes + 0x58);
            const std::uint8_t updatedPhase = *(bytes + 0x68);
            const unsigned int tick =
                FrontEndContext().mainMenuTickCount.fetch_add(
                    1,
                    std::memory_order_acq_rel) + 1;
            const unsigned int previousPhase =
                FrontEndContext().mainMenuLastLoggedPhase.exchange(
                    updatedPhase,
                    std::memory_order_acq_rel);
            if (tick <= 3 || previousPhase != updatedPhase ||
                (!FrontEndContext().fixtureStartInvoked.load(std::memory_order_acquire) &&
                    (updatedFlags & 4u) != 0))
            {
                char detail[256] = {};
                std::snprintf(
                    detail,
                    std::size(detail),
                    "tick=%u object=%p flags=0x%08lX phase=%u field78=0x%08lX field7C=0x%08lX start_invoked=%s",
                    tick,
                    object,
                    static_cast<unsigned long>(updatedFlags),
                    static_cast<unsigned int>(updatedPhase),
                    static_cast<unsigned long>(
                        *reinterpret_cast<const std::uint32_t*>(bytes + 0x78)),
                    static_cast<unsigned long>(
                        *reinterpret_cast<const std::uint32_t*>(bytes + 0x7C)),
                    FrontEndContext().fixtureStartInvoked.load(std::memory_order_acquire)
                        ? "true"
                        : "false");
                LogEvent("FixtureMainMenuTickProgress", detail);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            FrontEndContext().fixtureStartInvoked.store(true, std::memory_order_release);
            LogEvent("ClientFailed", "fixture main-menu Continue path raised a structured exception");
        }
    }

    void __stdcall ObserveFrontEndStartDoTick(void* object, const void* frame)
    {
        TryPassFrontEndStart(object, frame, "native front-end tick");
    }

    void OnFrontEndStartInitialized(void* object)
    {
        TryPassFrontEndStart(object, nullptr, "completed native front-end initialization");
    }

    void OnGameThreadIdle()
    {
        const HANDLE shutdownEvent = CoreContext().configuration.ShutdownEvent();
        if (shutdownEvent != nullptr &&
            WaitForSingleObject(shutdownEvent, 0) == WAIT_OBJECT_0 &&
            !FrontEndContext().shutdownStarted.exchange(true, std::memory_order_acq_rel))
        {
            Log("Automation: run-scoped shutdown event received; posting WM_QUIT.");
            LogEvent("ShutdownStarted", "run-scoped-event");
            // This is a launcher-owned disposable local test process. Calling
            // ExitProcess here can deadlock in third-party DLL detach handlers
            // when the harness deliberately keeps a background DX9 window in
            // its active-rendering state. Terminate only this known PID without
            // running the retail process's unrelated unload handlers.
            TerminateProcess(GetCurrentProcess(), ERROR_SUCCESS);
        }

        for (auto& candidate : FrontEndContext().startObjects)
        {
            TryPassFrontEndStart(
                candidate.load(std::memory_order_acquire),
                nullptr,
                "drained game-thread message queue");
            if (FrontEndContext().passStartStage.load(std::memory_order_acquire) != 0)
            {
                break;
            }
        }
        DriveBootstrapFixtureProbe();
        ObserveBootstrapHeroReadiness();
        DriveSaveListObservation();
        ObserveSaveListReadiness();
        DriveFixtureLoad();
        GameplayContext().runtime.RequestAutomationIdle();
        if (GameplayContext().runtime.ConsumeWorldDeparture())
        {
            // SCRIPT_NAME_HERO is reconstructed for the destination map. The
            // previous readiness edge represented the departing world.
            CharacterSnapshotContext().heroReadyLogged.store(false, std::memory_order_release);
            CharacterSnapshotContext().heroLastProbeAt.store(0, std::memory_order_release);
        }
    }

}

namespace
{
    using namespace fable::core::bootstrap;

    bool FrontEndFeatureEnabled(const FeatureContext&) noexcept { return true; }

    bool InstallFrontEndFeature(FeatureContext& context) noexcept
    {
        if (CoreContext().configuration.Mode() !=
            fable::automation::runtime::ClientMode::Observe ||
            !ScenarioUsesFrontEndStartAutomation())
        {
            return true;
        }

        const fable::core::Diagnostics diagnostics = {ScriptLog, ScriptEvent};
        if (IsPreResumeStage(context))
        {
            return true;
        }

        const fable::ui::front_end::FrontEndLifecycleCallbacks callbacks = {
            fable::automation::front_end::ObserveUiPageDoBegin,
            fable::automation::front_end::ObserveUiPageDoInit,
            fable::automation::front_end::ObserveUiPageStartPlay,
            fable::automation::front_end::ObservePlayLoadMapMovie,
            fable::automation::front_end::ObserveFrontEndStartDoInit,
            fable::automation::front_end::ObserveFrontEndStartDoTick,
        };
        auto& hooks = NativeHooksContext();
        const bool installed = hooks.frontEndLifecycle.Install(
                CoreContext().gameModule, diagnostics, callbacks) &&
            hooks.frontEndInitializer.Install(
                CoreContext().gameModule,
                diagnostics,
                fable::automation::front_end::OnFrontEndStartInitialized);
        if (!installed)
        {
            hooks.frontEndInitializer.Shutdown();
            hooks.frontEndLifecycle.Shutdown();
        }
        return installed;
    }

    void UninstallFrontEndFeature(FeatureContext& context) noexcept
    {
        if (IsPreResumeStage(context))
        {
            return;
        }
        NativeHooksContext().frontEndInitializer.Shutdown();
        NativeHooksContext().frontEndLifecycle.Shutdown();
    }

    FABLE_FEATURE_DEPENDENCIES(frontEndDependencies, "native.entity-world-hooks");
    FABLE_FEATURE_DESCRIPTOR(
        fableFrontEndAutomationFeature,
        "automation.frontend",
        "Front-end automation",
        FeaturePhase::Runtime,
        40,
        FrontEndFeatureEnabled,
        frontEndDependencies,
        std::size(frontEndDependencies),
        InstallFrontEndFeature,
        UninstallFrontEndFeature,
        "front-end-automation-installation");
}
