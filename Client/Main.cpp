#include <Windows.h>

#include "Automation/AppearanceCycle/AppearanceCycleScenario.h"
#include "Automation/CharacterSnapshot/ServerCharacterSnapshot.h"
#include "Automation/FixtureDocuments/Hooks/DocumentsFolderRedirectHook.h"
#include "Automation/Runtime/RuntimeConfiguration.h"
#include "Core/Diagnostics/DiagnosticLog.h"
#include "Core/GameThread/Hooks/GameThreadIdleHook.h"
#include "Game/Creature/AI/Hooks/AiBrainUpdateObserver.h"
#include "Game/Creature/Hooks/CreatureConstructorHook.h"
#include "Game/Creature/Locomotion/Hooks/CreatureModeManagerObserver.h"
#include "Game/Creature/Locomotion/Hooks/FollowCreatureActionHook.h"
#include "Game/Creature/Locomotion/Hooks/PhysicsNavigatorObserver.h"
#include "Game/HeroPawn/TransformProbe/Hooks/HeroTransformCompatibilityHooks.h"
#include "Scripting/Runtime/Host/ScriptHost.h"
#include "UI/FrontEnd/Hooks/FrontEndLifecycleHooks.h"
#include "UI/FrontEnd/Hooks/FrontEndStartInitializerHook.h"
#include "UI/MainWindow/MainWindowHook.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

namespace
{
    constexpr wchar_t kClientVersion[] = L"development-current";
    constexpr UINT_PTR kHotkeyTimerId = 0xFAB1;
    constexpr UINT kHotkeyPollIntervalMilliseconds = 16;

    // Supported executable:
    // SHA-256 2a95eea3c2cce9b47ca0f454a605b6952216f5d25158efd12ba48b70130989f2
    constexpr DWORD kExpectedTimestamp = 0x545D058C;
    constexpr DWORD kExpectedImageSize = 0x035D5000;
    constexpr DWORD kExpectedEntryPointRva = 0x0236A782;

    // Target-specific RVAs recovered from Fable Anniversary's native image.
    constexpr std::uintptr_t kGameScriptInterfaceSlotRva = 0x031BBC34;
    constexpr std::uintptr_t kGameScriptInterfaceVtableRva = 0x02AE35C4;
    constexpr std::uintptr_t kCharStringConstructorRva = 0x012B7800;
    constexpr std::uintptr_t kCharStringDestructorRva = 0x012B75D0;
    constexpr std::uintptr_t kGetHeroRva = 0x01889940;
    constexpr std::uintptr_t kGetThingWithScriptNameRva = 0x0189DF10;
    constexpr std::uintptr_t kTurnCreatureIntoRva = 0x01898200;
    constexpr std::uintptr_t kScriptThingVtableRva = 0x02A5CBF4;
    constexpr std::uintptr_t kScriptThingDestructorRva = 0x0135C7A7;
    constexpr std::uintptr_t kScriptThingIsNullRva = 0x0135B9E0;
    constexpr std::uintptr_t kScriptThingGetPositionVectorRva = 0x0135B93F;
    constexpr std::uintptr_t kScriptThingGetFacingAngleRva = 0x0135B994;
    constexpr std::uintptr_t kActiveHeroSelectorRva = 0x018FD7D0;
    constexpr std::uintptr_t kActiveHeroCreatureResolverRva = 0x018E3AD0;
    constexpr std::uintptr_t kHeroProgressionHealthGetMaximumRva = 0x019CC6DA;
    constexpr std::uintptr_t kGameScriptInterfaceTeleportThingRva = 0x0189EE20;
    constexpr std::uintptr_t kRegionManagerResolveIndexRva = 0x01BC6560;
    constexpr std::uintptr_t kThingPlayerCreatureVtableRva = 0x02B1DBB4;
    constexpr std::uintptr_t kThingCreatureVtableRva = 0x02B1AFE4;
    constexpr std::uintptr_t kThingPlayerCreatureModifyCombatHealthRva = 0x01B5A520;
    constexpr std::uintptr_t kFrontEndMainMenuVtableRva = 0x02B302A4;
    constexpr std::uintptr_t kFrontEndMainMenuDoBeginRva = 0x01BEFC00;
    constexpr std::uintptr_t kFrontEndMainMenuDoTickRva = 0x01BF05A0;
    constexpr std::uintptr_t kFrontEndMainMenuDoOnUIEventRva = 0x01BF05F0;
    constexpr std::uintptr_t kLoadGamePageVtableRva = 0x02B30D84;
    constexpr std::uintptr_t kLoadGamePageDoBeginRva = 0x01BF4F60;
    constexpr std::uintptr_t kLoadGamePageDoTickRva = 0x01BF6510;
    constexpr std::uintptr_t kLoadGamePageDoStartPlayRva = 0x01BF56C0;
    constexpr std::uintptr_t kLoadGamePageDoOnUIEventRva = 0x01BF5C20;
    constexpr std::uintptr_t kLocalSaveManagerSlotRva = 0x0322FC00;

    constexpr std::size_t kGetHeroVtableIndex = 70;
    constexpr std::size_t kGetThingWithScriptNameVtableIndex = 78;
    constexpr std::size_t kTurnCreatureIntoVtableIndex = 100;
    constexpr std::size_t kScriptThingDestructorVtableIndex = 0;
    constexpr std::size_t kScriptThingIsNullVtableIndex = 77;
    constexpr std::size_t kThingCreatureModifyCombatHealthVtableIndex = 0x100 / sizeof(void*);
    constexpr std::size_t kTeleportThingVtableIndex = 0x7B0 / sizeof(void*);

    constexpr std::array<const char*, 14> kCreatureCycle = {
        "CREATURE_HERO",
        "CREATURE_BS_GUARD",
        "CREATURE_BS_GUARD_CROSSBOW",
        "CREATURE_PRISON_GUARD",
        "CREATURE_KN_GUARD",
        "CREATURE_BS_VILLAGER_MALE",
        "CREATURE_BS_VILLAGER_FEMALE",
        "CREATURE_TRADER_01",
        "CREATURE_BANDIT_GRUNT",
        "CREATURE_RIVAL_HERO_WHISPER",
        "CREATURE_RIVAL_HERO_THUNDER",
        "CREATURE_HOBBE_GRUNT",
        "CREATURE_BALVERINE_EASY",
        "CREATURE_HERO_CHILD",
    };

    struct CountedPointerInfo
    {
        unsigned int referenceCount;
        void(__fastcall* deleteFunction)(void*);
        void* data;
    };

    struct ScriptThing
    {
        void** vtable;
        void* implementation;
        CountedPointerInfo* pointerInfo;
    };

    struct CharString
    {
        void* stringData;
    };

    struct GameScriptInterface
    {
        void** vtable;
    };

    using CharStringConstructor = void(__thiscall*)(CharString*, const char*, int);
    using CharStringDestructor = void(__thiscall*)(CharString*);
    using GetThingWithScriptName = ScriptThing* (__thiscall*)(
        GameScriptInterface*, ScriptThing*, const CharString*);
    using TurnCreatureInto = ScriptThing* (__thiscall*)(
        GameScriptInterface*, ScriptThing*, const ScriptThing*, const CharString*);
    using ScriptThingDestructor = void* (__thiscall*)(ScriptThing*, unsigned int);
    using ScriptThingIsNull = bool(__thiscall*)(ScriptThing*);
    using ScriptThingGetPositionVector = const float* (__thiscall*)(ScriptThing*);
    using ScriptThingGetFacingAngle = float(__thiscall*)(ScriptThing*);
    using ActiveHeroSelector = void* (__thiscall*)(void*);
    using ActiveHeroCreatureResolver = void* (__thiscall*)(void*);
    using HeroProgressionHealthGetMaximum = int(__thiscall*)(void*);
    using TeleportThing = void(__thiscall*)(
        GameScriptInterface*, const ScriptThing*, const float*, float, bool, int);
    using ModifyCombatHealth = void(__thiscall*)(void*, float, bool);
    using GetRegionManager = void* (__thiscall*)(void*);
    using GetRegionAtPosition = std::uint32_t(__thiscall*)(void*, const float*);
    using ResolveRegionIndex = int(__thiscall*)(void*, std::uint32_t);

    using ServerCharacterSnapshot =
        fable::automation::character_snapshot::ServerCharacterSnapshot;
    using AppearanceCycleCharacterSnapshot =
        fable::automation::appearance_cycle::CharacterSnapshot;

    struct CharacterState
    {
        float position[3] = {};
        float facingAngle = 0.0f;
        int progressionHealthValue = 0;
        int progressionHealthMaximum = 0;
        float combatHealth = 0.0f;
        float combatHealthMaximum = 0.0f;
        int regionIndex = 0;
        void* creature = nullptr;
        void* creatureVtable = nullptr;
    };

    struct CreatureComponentEntry
    {
        std::uint32_t type;
        void* component;
    };

    enum class TransformResult
    {
        Succeeded,
        GameNotReady,
        InvalidInterface,
        HeroUnavailable,
        EngineRejected,
        RetentionLimit,
        StructuredException,
    };

    enum class PendingTransform
    {
        None,
        Cycle,
        Restore,
    };

    using ClientMode = fable::automation::runtime::ClientMode;

    struct NativeFault
    {
        DWORD code = ERROR_SUCCESS;
        void* instructionAddress = nullptr;
        ULONG_PTR operation = 0;
        void* accessedAddress = nullptr;
        const char* stage = "unknown";
    };

    HMODULE g_clientModule = nullptr;
    HMODULE g_gameModule = nullptr;
    fable::core::DiagnosticLog g_diagnosticLog;
    fable::automation::runtime::RuntimeConfiguration g_runtimeConfiguration;
    fable::automation::appearance_cycle::AppearanceCycleScenario
        g_appearanceCycleScenario;
    fable::automation::fixture_documents::DocumentsFolderRedirectHook
        g_documentsFolderRedirectHook;
    fable::core::game_thread::GameThreadIdleHook g_gameThreadIdleHook;
    fable::game::creature::ai::AiBrainUpdateObserver g_aiBrainUpdateObserver;
    fable::game::creature::CreatureConstructorHook g_creatureConstructorHook;
    fable::game::creature::locomotion::CreatureModeManagerObserver
        g_creatureModeManagerObserver;
    fable::game::creature::locomotion::FollowCreatureActionHook
        g_followCreatureActionHook;
    fable::game::creature::locomotion::PhysicsNavigatorObserver
        g_physicsNavigatorObserver;
    fable::game::hero_pawn::transform_probe::HeroTransformCompatibilityHooks
        g_heroTransformCompatibilityHooks;
    fable::scripting::ScriptHost g_scriptHost;
    fable::ui::front_end::FrontEndLifecycleHooks g_frontEndLifecycleHooks;
    fable::ui::front_end::FrontEndStartInitializerHook
        g_frontEndStartInitializerHook;
    HWND g_gameWindow = nullptr;
    DWORD g_gameWindowThreadId = 0;
    fable::ui::MainWindowHook g_mainWindowHook;
    std::atomic_size_t g_creatureIndex{0};
    std::array<ScriptThing, 64> g_retainedTransformHandles = {};
    std::size_t g_retainedTransformHandleCount = 0;
    bool g_oneKeyIsDown = false;
    bool g_reloadKeyIsDown = false;
    bool g_firstTimerTickLogged = false;
    std::atomic<PendingTransform> g_pendingTransform{PendingTransform::None};
    std::atomic_uint g_lowAddressAccessViolationsLogged{0};
    std::atomic_uint g_uiLifecycleEventsLogged{0};
    std::atomic_bool g_frontendReadyLogged{false};
    std::atomic<ULONGLONG> g_frontendReadyAt{0};
    std::atomic<void*> g_frontEndMainMenuObject{nullptr};
    std::atomic<ULONGLONG> g_bootstrapMainMenuLastTickAt{0};
    std::atomic_uint g_bootstrapMainMenuTickCount{0};
    std::atomic_uint g_bootstrapMainMenuLastLoggedPhase{0xFFFFFFFFu};
    std::atomic_bool g_bootstrapMainMenuReady{false};
    std::atomic_bool g_bootstrapStartInvoked{false};
    std::atomic<ULONGLONG> g_bootstrapStartInvokedAt{0};
    std::atomic<ULONGLONG> g_bootstrapHeroLastProbeAt{0};
    std::atomic_uint g_bootstrapHeroProbeCount{0};
    std::atomic_bool g_bootstrapHeroReadyLogged{false};
    std::atomic_uint g_characterStateStableSamples{0};
    std::atomic_bool g_characterStateAssertionPassed{false};
    std::atomic_bool g_characterSnapshotApplied{false};
    std::atomic_bool g_characterSnapshotAssertionPassed{false};
    std::atomic_uint g_characterSnapshotVerificationAttempts{0};
    CharacterState g_characterSnapshotBaselineState;
    unsigned int g_characterSnapshotBaselineStableSamples = 0;
    std::atomic<void*> g_loadGamePageObject{nullptr};
    std::atomic_bool g_saveListRequested{false};
    std::atomic_bool g_saveListReadyLogged{false};
    std::atomic_bool g_saveListBeginAttempted{false};
    std::atomic<ULONGLONG> g_saveListLastTickAt{0};
    std::atomic_uint g_saveListTickCount{0};
    std::atomic_uint g_saveListLastLoggedPhase{0xFFFFFFFFu};
    std::atomic_bool g_loadFixtureAutoSaveSelected{false};
    std::atomic_uint g_loadFixtureAutoSaveIdentity{0xFFFFFFFFu};
    std::atomic<ULONGLONG> g_loadFixtureAutoSaveSelectedAt{0};
    std::atomic_bool g_loadFixtureStartInvoked{false};
    std::atomic<ULONGLONG> g_loadFixtureStartInvokedAt{0};
    std::atomic_bool g_loadFixtureMainMenuReleased{false};
    std::array<std::atomic<void*>, 4> g_frontEndStartObjects = {};
    // 0 = idle, 1 = Enter is held down, 2 = Enter was released.  The title
    // screen polls DirectInput, so a complete down/up pair in one callback is
    // too easy for it to miss.
    std::atomic_uint g_automationPassStartStage{0};
    std::atomic_uint g_automationPassStartAttempts{0};
    std::atomic<ULONGLONG> g_frontEndStartReadyAt{0};
    std::atomic<ULONGLONG> g_automationPassStartKeyDownAt{0};
    std::atomic<ULONGLONG> g_automationPassStartSubmittedAt{0};
    std::atomic_bool g_automationShutdownStarted{false};
    unsigned int g_interfaceProbeTicks = 0;
    int g_lastInterfaceReadyState = -1;
    int g_lastSteamApiReadyState = -1;
    ServerCharacterSnapshot g_characterSnapshot;
    bool g_characterSnapshotConfigured = false;

    std::string WideToUtf8(const wchar_t* value);
    bool ResolveGameInterface(GameScriptInterface*& gameInterface);
    bool RetainTransformHandle(ScriptThing& thing, const char* label);
    bool ScenarioIs(const wchar_t* value)
    {
        return g_runtimeConfiguration.ScenarioIs(value);
    }

    bool ScenarioUsesFrontEndStartAutomation()
    {
        return g_runtimeConfiguration.UsesFrontEndStartAutomation();
    }

    bool ScenarioLoadsFixture()
    {
        return g_runtimeConfiguration.LoadsFixture();
    }

    template <std::size_t Size>
    bool BytesMatch(const std::uint8_t* address, const std::array<std::uint8_t, Size>& expected)
    {
        return std::memcmp(address, expected.data(), expected.size()) == 0;
    }

    void Log(const char* message)
    {
        g_diagnosticLog.Log(message);
    }

    void LogFormat(const char* format, ...)
    {
        char message[1024] = {};
        va_list arguments;
        va_start(arguments, format);
        std::vsnprintf(message, std::size(message), format, arguments);
        va_end(arguments);
        Log(message);
    }

    void LogEvent(const char* state, const char* detail = "")
    {
        g_diagnosticLog.Event(state, detail);
    }

    void ScriptLog(const char* message)
    {
        LogFormat("Script: %s", message != nullptr ? message : "<null>");
    }

    void ScriptEvent(const char* state, const char* detail)
    {
        g_appearanceCycleScenario.ObserveScriptEvent(state);
        LogEvent(
            state != nullptr ? state : "ScriptEvent",
            detail != nullptr ? detail : "");
    }

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

        std::string converted(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            value,
            -1,
            converted.data(),
            required,
            nullptr,
            nullptr);
        converted.pop_back();
        return converted;
    }


    bool InitializeCharacterSnapshot()
    {
        if (g_runtimeConfiguration.CharacterSnapshotPath().empty())
        {
            return true;
        }
        if (!ScenarioLoadsFixture())
        {
            LogEvent("ClientFailed", "character-snapshot-requires-fixture-load-scenario");
            return false;
        }

        std::string failure;
        if (!fable::automation::character_snapshot::
                ServerCharacterSnapshotLoader::Load(
                    g_runtimeConfiguration.CharacterSnapshotPath().c_str(),
                    g_characterSnapshot,
                    failure))
        {
            LogEvent("ClientFailed", failure.c_str());
            return false;
        }

        g_characterSnapshotConfigured = true;
        char detail[512] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "server_character_id=%s display_name=%s bootstrap_save=%s region_index=%d position=(%.6f,%.6f,%.6f) facing=%.6f combat_health=%.3f",
            g_characterSnapshot.serverCharacterId.c_str(),
            g_characterSnapshot.displayName.c_str(),
            g_characterSnapshot.bootstrapSave.c_str(),
            g_characterSnapshot.regionIndex,
            g_characterSnapshot.position[0],
            g_characterSnapshot.position[1],
            g_characterSnapshot.position[2],
            g_characterSnapshot.facingAngle,
            g_characterSnapshot.combatHealth);
        LogEvent("CharacterSnapshotReady", detail);
        LogFormat("Server character snapshot accepted: %s (%s).",
            g_characterSnapshot.displayName.c_str(),
            g_characterSnapshot.serverCharacterId.c_str());
        return true;
    }

    void LogStartupContext()
    {
        wchar_t currentDirectory[32'768] = {};
        wchar_t steamAppId[64] = {};
        GetCurrentDirectoryW(static_cast<DWORD>(std::size(currentDirectory)), currentDirectory);
        GetEnvironmentVariableW(L"SteamAppId", steamAppId, static_cast<DWORD>(std::size(steamAppId)));

        LogFormat(
            "Startup: build=%ls pid=%lu bootstrap_tid=%lu game_base=%p client_base=%p.",
            kClientVersion,
            static_cast<unsigned long>(GetCurrentProcessId()),
            static_cast<unsigned long>(GetCurrentThreadId()),
            GetModuleHandleW(nullptr),
            g_clientModule);
        LogFormat("Startup: current_directory=%s.", WideToUtf8(currentDirectory).c_str());
        LogFormat("Startup: command_line=%s.", WideToUtf8(GetCommandLineW()).c_str());
        LogFormat(
            "Startup: SteamAppId=%s steam_api=%s.",
            WideToUtf8(steamAppId).empty() ? "<unset>" : WideToUtf8(steamAppId).c_str(),
            GetModuleHandleW(L"steam_api.dll") == nullptr ? "not-loaded" : "loaded");
        LogFormat(
            "Startup: mode=%s run_id=%s scenario=%s events=%s.",
            g_runtimeConfiguration.Mode() == ClientMode::TransformProbe
                ? "transform_probe"
                : "observe",
            g_runtimeConfiguration.RunId().empty()
                ? "<unset>"
                : WideToUtf8(g_runtimeConfiguration.RunId().c_str()).c_str(),
            g_runtimeConfiguration.Scenario().empty()
                ? "<none>"
                : WideToUtf8(g_runtimeConfiguration.Scenario().c_str()).c_str(),
            g_runtimeConfiguration.EventPath().empty()
                ? "<unset>"
                : WideToUtf8(g_runtimeConfiguration.EventPath().c_str()).c_str());
    }

    LONG CaptureNativeFault(
        EXCEPTION_POINTERS* exceptionPointers,
        NativeFault* fault,
        const char* stage)
    {
        fault->stage = stage;
        if (exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr)
        {
            const EXCEPTION_RECORD* record = exceptionPointers->ExceptionRecord;
            fault->code = record->ExceptionCode;
            fault->instructionAddress = record->ExceptionAddress;
            if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
                record->NumberParameters >= 2)
            {
                fault->operation = record->ExceptionInformation[0];
                fault->accessedAddress = reinterpret_cast<void*>(record->ExceptionInformation[1]);
            }
        }
        return EXCEPTION_EXECUTE_HANDLER;
    }

    void LogNativeFault(const NativeFault& fault)
    {
        const auto base = reinterpret_cast<std::uintptr_t>(g_gameModule);
        const auto instruction = reinterpret_cast<std::uintptr_t>(fault.instructionAddress);
        const bool isInsideGame = instruction >= base &&
            instruction < base + kExpectedImageSize;

        wchar_t modulePath[MAX_PATH] = {};
        MEMORY_BASIC_INFORMATION memory = {};
        if (fault.instructionAddress != nullptr &&
            VirtualQuery(fault.instructionAddress, &memory, sizeof(memory)) == sizeof(memory))
        {
            GetModuleFileNameW(
                static_cast<HMODULE>(memory.AllocationBase),
                modulePath,
                static_cast<DWORD>(std::size(modulePath)));
        }

        std::string gameRva = "<outside-game>";
        if (isInsideGame)
        {
            char buffer[32] = {};
            std::snprintf(
                buffer,
                std::size(buffer),
                "0x%08lX",
                static_cast<unsigned long>(instruction - base));
            gameRva = buffer;
        }
        std::string module = WideToUtf8(modulePath);
        if (module.empty())
        {
            module = "<unknown>";
        }

        LogFormat(
            "Native fault: stage=%s code=0x%08lX instruction=%p game_rva=%s module=%s.",
            fault.stage,
            static_cast<unsigned long>(fault.code),
            fault.instructionAddress,
            gameRva.c_str(),
            module.c_str());

        if (fault.code == EXCEPTION_ACCESS_VIOLATION)
        {
            const char* operation = fault.operation == 0
                ? "read"
                : fault.operation == 1 ? "write" : fault.operation == 8 ? "execute" : "unknown";
            LogFormat(
                "Native fault: access_violation operation=%s address=%p.",
                operation,
                fault.accessedAddress);
        }
    }

    LONG CALLBACK ObserveProcessException(EXCEPTION_POINTERS* exceptionPointers)
    {
        if (exceptionPointers == nullptr || exceptionPointers->ExceptionRecord == nullptr)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const EXCEPTION_RECORD* record = exceptionPointers->ExceptionRecord;
        if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
            record->NumberParameters < 2 ||
            record->ExceptionInformation[1] >= 0x1'0000)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const unsigned int observation =
            g_lowAddressAccessViolationsLogged.fetch_add(1, std::memory_order_relaxed) + 1;
        if (observation > 8)
        {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        NativeFault fault = {};
        fault.stage = "process-wide low-address access-violation observer";
        fault.code = record->ExceptionCode;
        fault.instructionAddress = record->ExceptionAddress;
        fault.operation = record->ExceptionInformation[0];
        fault.accessedAddress = reinterpret_cast<void*>(record->ExceptionInformation[1]);

        LogFormat(
            "Process exception observer: event=%u thread=%lu; continuing Windows exception dispatch.",
            observation,
            static_cast<unsigned long>(GetCurrentThreadId()));
        LogNativeFault(fault);

#if defined(_M_IX86)
        if (exceptionPointers->ContextRecord != nullptr)
        {
            const CONTEXT* context = exceptionPointers->ContextRecord;
            LogFormat(
                "Process exception registers: eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX eip=%08lX.",
                context->Eax,
                context->Ebx,
                context->Ecx,
                context->Edx,
                context->Esi,
                context->Edi,
                context->Ebp,
                context->Esp,
                context->Eip);
        }
#endif

        return EXCEPTION_CONTINUE_SEARCH;
    }

    void LogUiLifecycleEvent(
        const char* state,
        const void* object,
        const void* frame,
        std::size_t virtualMethodIndex)
    {
        const unsigned int observation =
            g_uiLifecycleEventsLogged.fetch_add(1, std::memory_order_relaxed) + 1;
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
            reinterpret_cast<std::uintptr_t>(g_gameModule) + kFrontEndMainMenuDoBeginRva);
        const auto expectedLoadGameDoBegin = reinterpret_cast<void*>(
            reinterpret_cast<std::uintptr_t>(g_gameModule) + kLoadGamePageDoBeginRva);
        if (std::strcmp(state, "UiPageDoBegin") == 0 &&
            implementation == expectedFrontEndDoBegin &&
            !g_frontendReadyLogged.exchange(true, std::memory_order_acq_rel))
        {
            g_frontEndMainMenuObject.store(
                const_cast<void*>(object),
                std::memory_order_release);
            g_frontendReadyAt.store(GetTickCount64(), std::memory_order_release);
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
            !g_saveListRequested.exchange(true, std::memory_order_acq_rel))
        {
            g_loadGamePageObject.store(
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
                reinterpret_cast<std::uintptr_t>(g_gameModule) + kLoadGamePageVtableRva);
            if (object != nullptr && *reinterpret_cast<void* const*>(object) == expectedVtable)
            {
                g_loadGamePageObject.store(
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
        g_frontEndStartReadyAt.compare_exchange_strong(
            expectedReadyAt,
            GetTickCount64(),
            std::memory_order_acq_rel,
            std::memory_order_acquire);
        for (auto& candidate : g_frontEndStartObjects)
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
            g_automationPassStartStage.load(std::memory_order_acquire);
        const ULONGLONG now = GetTickCount64();
        if (inputStage == 1)
        {
            const ULONGLONG keyDownAt =
                g_automationPassStartKeyDownAt.load(std::memory_order_acquire);
            if (now - keyDownAt < 150)
            {
                return;
            }

            INPUT input = {};
            input.type = INPUT_KEYBOARD;
            input.ki.wScan = static_cast<WORD>(
                MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC));
            input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
            if (SendInput(1, &input, sizeof(input)) != 1)
            {
                LogFormat(
                    "Automation: could not release the start-screen Enter input; error=%lu.",
                    static_cast<unsigned long>(GetLastError()));
                LogEvent("ClientFailed", "front-end-input-release-failed");
                return;
            }

            g_automationPassStartStage.store(2, std::memory_order_release);
            g_automationPassStartSubmittedAt.store(now, std::memory_order_release);
            LogEvent("FrontEndInputSubmitted", source);
            return;
        }
        if (inputStage == 2)
        {
            const ULONGLONG submittedAt =
                g_automationPassStartSubmittedAt.load(std::memory_order_acquire);
            if (!g_frontendReadyLogged.load(std::memory_order_acquire) &&
                now - submittedAt >= 2'000 &&
                g_automationPassStartAttempts.load(std::memory_order_acquire) < 5)
            {
                g_automationPassStartStage.store(0, std::memory_order_release);
                LogEvent("FrontEndInputRetry", "title screen still active");
            }
            return;
        }
        if (inputStage != 0)
        {
            return;
        }

        const ULONGLONG readyAt =
            g_frontEndStartReadyAt.load(std::memory_order_acquire);
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
        if (!g_automationPassStartStage.compare_exchange_strong(
                expectedStage,
                3,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            return;
        }
        const unsigned int attempt =
            g_automationPassStartAttempts.fetch_add(1, std::memory_order_acq_rel) + 1;

        LogUiLifecycleEvent("FrontEndStartAutomationTick", object, frame, 0);
        LogFormat(
            "Automation: submitting Enter attempt %u through Fable's DirectInput path from %s; object=%p state_flags=0x%08lX foreground=%p game_window=%p.",
            attempt,
            source,
            object,
            static_cast<unsigned long>(stateFlags),
            GetForegroundWindow(),
            g_gameWindow);

        if (GetForegroundWindow() != g_gameWindow)
        {
            SetForegroundWindow(g_gameWindow);
        }

        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wScan = static_cast<WORD>(
            MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC));
        input.ki.dwFlags = KEYEVENTF_SCANCODE;
        if (SendInput(1, &input, sizeof(input)) != 1)
        {
            LogFormat(
                "Automation: could not press the start-screen Enter input; error=%lu.",
                static_cast<unsigned long>(GetLastError()));
            g_automationPassStartStage.store(2, std::memory_order_release);
            LogEvent("ClientFailed", "front-end-input-press-failed");
            return;
        }
        g_automationPassStartKeyDownAt.store(GetTickCount64(), std::memory_order_release);
        g_automationPassStartStage.store(1, std::memory_order_release);
        LogEvent("FrontEndInputPressed", source);
    }

    void DriveBootstrapFixtureProbe()
    {
        if (!ScenarioIs(L"bootstrap_fixture_probe") ||
            !g_frontendReadyLogged.load(std::memory_order_acquire) ||
            g_bootstrapStartInvoked.load(std::memory_order_acquire))
        {
            return;
        }

        void* const object =
            g_frontEndMainMenuObject.load(std::memory_order_acquire);
        if (object == nullptr)
        {
            return;
        }

        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(object);
            void* const vtable = *reinterpret_cast<void* const*>(object);
            void* const expectedVtable = reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(g_gameModule) +
                kFrontEndMainMenuVtableRva);
            if (vtable != expectedVtable)
            {
                LogEvent("ClientFailed", "bootstrap-main-menu-vtable-validation-failed");
                g_bootstrapStartInvoked.store(true, std::memory_order_release);
                return;
            }

            const std::uint32_t flags =
                *reinterpret_cast<const std::uint32_t*>(bytes + 0x58);
            const std::uint8_t phase = *(bytes + 0x68);
            if ((flags & 4u) == 0)
            {
                const ULONGLONG now = GetTickCount64();
                ULONGLONG previousTick =
                    g_bootstrapMainMenuLastTickAt.load(std::memory_order_acquire);
                if (now - previousTick < 16 ||
                    !g_bootstrapMainMenuLastTickAt.compare_exchange_strong(
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
                    reinterpret_cast<std::uintptr_t>(g_gameModule) +
                    kFrontEndMainMenuDoTickRva);
                if (implementation != expectedImplementation)
                {
                    LogEvent("ClientFailed", "bootstrap-main-menu-DoTick-validation-failed");
                    g_bootstrapStartInvoked.store(true, std::memory_order_release);
                    return;
                }

                using MainMenuDoTick = void(__thiscall*)(void*, float);
                reinterpret_cast<MainMenuDoTick>(implementation)(object, 1.0f / 60.0f);
                const std::uint32_t updatedFlags =
                    *reinterpret_cast<const std::uint32_t*>(bytes + 0x58);
                const std::uint8_t updatedPhase = *(bytes + 0x68);
                const unsigned int tick =
                    g_bootstrapMainMenuTickCount.fetch_add(
                        1,
                        std::memory_order_acq_rel) + 1;
                const unsigned int previousPhase =
                    g_bootstrapMainMenuLastLoggedPhase.exchange(
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

            if (!g_bootstrapMainMenuReady.exchange(true, std::memory_order_acq_rel))
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
                reinterpret_cast<std::uintptr_t>(g_gameModule) +
                kFrontEndMainMenuDoOnUIEventRva);
            if (implementation != expectedImplementation)
            {
                LogEvent("ClientFailed", "bootstrap-main-menu-OnUIEvent-validation-failed");
                g_bootstrapStartInvoked.store(true, std::memory_order_release);
                return;
            }

            g_bootstrapStartInvoked.store(true, std::memory_order_release);
            g_bootstrapStartInvokedAt.store(GetTickCount64(), std::memory_order_release);
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
            g_bootstrapStartInvoked.store(true, std::memory_order_release);
            LogEvent("ClientFailed", "bootstrap-main-menu-native-call-raised-structured-exception");
        }
    }

    bool ReadCharacterState(
        GameScriptInterface* gameInterface,
        ScriptThing& hero,
        CharacterState& state,
        const char*& failure)
    {
        failure = "unknown";
        if (gameInterface == nullptr || g_gameModule == nullptr)
        {
            failure = "game-interface-unavailable";
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(g_gameModule);
        const auto getPosition = reinterpret_cast<ScriptThingGetPositionVector>(
            base + kScriptThingGetPositionVectorRva);
        const auto getFacing = reinterpret_cast<ScriptThingGetFacingAngle>(
            base + kScriptThingGetFacingAngleRva);
        const auto selectActiveHero = reinterpret_cast<ActiveHeroSelector>(
            base + kActiveHeroSelectorRva);
        const auto resolveCreature = reinterpret_cast<ActiveHeroCreatureResolver>(
            base + kActiveHeroCreatureResolverRva);
        const auto getProgressionHealthMaximum =
            reinterpret_cast<HeroProgressionHealthGetMaximum>(
                base + kHeroProgressionHealthGetMaximumRva);

        __try
        {
            const float* const position = getPosition(&hero);
            if (position == nullptr)
            {
                failure = "position-vector-unavailable";
                return false;
            }
            state.position[0] = position[0];
            state.position[1] = position[1];
            state.position[2] = position[2];
            state.facingAngle = getFacing(&hero);
            if (!std::isfinite(state.position[0]) ||
                !std::isfinite(state.position[1]) ||
                !std::isfinite(state.position[2]) ||
                !std::isfinite(state.facingAngle))
            {
                failure = "non-finite-transform";
                return false;
            }

            void* const regionOwner = *reinterpret_cast<void* const*>(
                reinterpret_cast<const std::uint8_t*>(gameInterface) + 0x04);
            if (regionOwner == nullptr)
            {
                failure = "region-manager-owner-unavailable";
                return false;
            }
            auto* const regionOwnerVtable = *reinterpret_cast<void***>(regionOwner);
            if (regionOwnerVtable == nullptr)
            {
                failure = "region-manager-owner-vtable-unavailable";
                return false;
            }
            const auto getRegionManager = reinterpret_cast<GetRegionManager>(
                regionOwnerVtable[0x30 / sizeof(void*)]);
            void* const regionManager = getRegionManager(regionOwner);
            if (regionManager == nullptr)
            {
                failure = "region-manager-unavailable";
                return false;
            }
            auto* const regionManagerVtable = *reinterpret_cast<void***>(regionManager);
            if (regionManagerVtable == nullptr)
            {
                failure = "region-manager-vtable-unavailable";
                return false;
            }
            const auto getRegionAtPosition = reinterpret_cast<GetRegionAtPosition>(
                regionManagerVtable[0x40 / sizeof(void*)]);
            const std::uint32_t regionToken =
                getRegionAtPosition(regionManager, state.position);
            const auto resolveRegionIndex = reinterpret_cast<ResolveRegionIndex>(
                base + kRegionManagerResolveIndexRva);
            state.regionIndex = resolveRegionIndex(regionManager, regionToken);
            if (state.regionIndex <= 0 || state.regionIndex > 1'024)
            {
                failure = "active-region-index-out-of-range";
                return false;
            }

            void* const selectorOwner = *reinterpret_cast<void* const*>(
                reinterpret_cast<const std::uint8_t*>(gameInterface) + 0x14);
            if (selectorOwner == nullptr)
            {
                failure = "active-hero-selector-unavailable";
                return false;
            }
            void* const selectedHero = selectActiveHero(selectorOwner);
            void* const creature = selectedHero == nullptr
                ? nullptr
                : resolveCreature(selectedHero);
            if (creature == nullptr)
            {
                failure = "active-hero-creature-unavailable";
                return false;
            }
            state.creature = creature;
            state.creatureVtable = *reinterpret_cast<void* const*>(creature);
            if (state.creatureVtable == nullptr)
            {
                failure = "active-hero-creature-vtable-unavailable";
                return false;
            }

            const auto expectedCreatureVtable = reinterpret_cast<void*>(
                base + kThingPlayerCreatureVtableRva);
            if (state.creatureVtable != expectedCreatureVtable)
            {
                failure = "active-hero-is-not-CThingPlayerCreature";
                return false;
            }

            const auto* const creatureBytes =
                static_cast<const std::uint8_t*>(creature);
            state.combatHealthMaximum = *reinterpret_cast<const float*>(
                creatureBytes + 0xCC);
            state.combatHealth = *reinterpret_cast<const float*>(
                creatureBytes + 0xD0);
            if (!std::isfinite(state.combatHealthMaximum) ||
                !std::isfinite(state.combatHealth) ||
                state.combatHealthMaximum <= 0.0f ||
                state.combatHealth < 0.0f ||
                state.combatHealth > state.combatHealthMaximum + 0.01f)
            {
                failure = "combat-health-values-out-of-range";
                return false;
            }

            const auto* const begin = *reinterpret_cast<CreatureComponentEntry* const*>(
                creatureBytes + 0x44);
            const auto* const end = *reinterpret_cast<CreatureComponentEntry* const*>(
                creatureBytes + 0x48);
            const auto beginAddress = reinterpret_cast<std::uintptr_t>(begin);
            const auto endAddress = reinterpret_cast<std::uintptr_t>(end);
            if (begin == nullptr || end == nullptr || endAddress < beginAddress ||
                (endAddress - beginAddress) % sizeof(CreatureComponentEntry) != 0)
            {
                failure = "creature-component-collection-invalid";
                return false;
            }
            const std::size_t componentCount =
                (endAddress - beginAddress) / sizeof(CreatureComponentEntry);
            if (componentCount > 256)
            {
                failure = "creature-component-collection-too-large";
                return false;
            }

            // Component type 4 is the Hero progression path named "Health" by
            // Fable's script API.  It is distinct from the creature's combat HP.
            void* progressionHealthComponent = nullptr;
            for (std::size_t index = 0; index < componentCount; ++index)
            {
                if (begin[index].type == 4)
                {
                    progressionHealthComponent = begin[index].component;
                    break;
                }
            }
            if (progressionHealthComponent == nullptr)
            {
                failure = "hero-progression-health-component-unavailable";
                return false;
            }

            state.progressionHealthValue = *reinterpret_cast<const int*>(
                static_cast<const std::uint8_t*>(progressionHealthComponent) + 0x30);
            state.progressionHealthMaximum =
                getProgressionHealthMaximum(progressionHealthComponent);
            if (state.progressionHealthMaximum <= 0 ||
                state.progressionHealthValue < -state.progressionHealthMaximum ||
                state.progressionHealthValue > state.progressionHealthMaximum)
            {
                failure = "hero-progression-health-values-out-of-range";
                return false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            failure = "native-character-state-read-raised-structured-exception";
            return false;
        }

        failure = nullptr;
        return true;
    }

    bool CharacterSnapshotBaselineIsStable(const CharacterState& state)
    {
        constexpr float kTransformTolerance = 0.01f;
        constexpr float kHealthTolerance = 0.01f;
        const bool sameAsPrevious =
            g_characterSnapshotBaselineStableSamples != 0 &&
            state.creature == g_characterSnapshotBaselineState.creature &&
            state.regionIndex == g_characterSnapshotBaselineState.regionIndex &&
            std::fabs(state.position[0] -
                g_characterSnapshotBaselineState.position[0]) <= kTransformTolerance &&
            std::fabs(state.position[1] -
                g_characterSnapshotBaselineState.position[1]) <= kTransformTolerance &&
            std::fabs(state.position[2] -
                g_characterSnapshotBaselineState.position[2]) <= kTransformTolerance &&
            std::fabs(state.facingAngle -
                g_characterSnapshotBaselineState.facingAngle) <= kTransformTolerance &&
            std::fabs(state.combatHealth -
                g_characterSnapshotBaselineState.combatHealth) <= kHealthTolerance &&
            std::fabs(state.combatHealthMaximum -
                g_characterSnapshotBaselineState.combatHealthMaximum) <= kHealthTolerance;

        g_characterSnapshotBaselineState = state;
        g_characterSnapshotBaselineStableSamples = sameAsPrevious
            ? g_characterSnapshotBaselineStableSamples + 1
            : 1;

        char detail[320] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "stable_samples=%u region_index=%d position=(%.6f,%.6f,%.6f) facing=%.6f combat_health=%.3f combat_health_maximum=%.3f creature=%p",
            g_characterSnapshotBaselineStableSamples,
            state.regionIndex,
            state.position[0],
            state.position[1],
            state.position[2],
            state.facingAngle,
            state.combatHealth,
            state.combatHealthMaximum,
            state.creature);
        LogEvent("CharacterSnapshotBaselineSample", detail);
        return g_characterSnapshotBaselineStableSamples >= 3;
    }

    bool ApplyCharacterSnapshot(
        GameScriptInterface* gameInterface,
        ScriptThing& hero,
        const CharacterState& before,
        const char*& failure)
    {
        failure = "unknown-character-snapshot-application-failure";
        if (!g_characterSnapshotConfigured || gameInterface == nullptr ||
            before.creature == nullptr || before.creatureVtable == nullptr)
        {
            failure = "character-snapshot-application-state-is-unavailable";
            return false;
        }
        if (g_characterSnapshot.combatHealth > before.combatHealthMaximum + 0.01f)
        {
            failure = "snapshot-combat-health-exceeds-loaded-maximum";
            return false;
        }
        if (g_characterSnapshot.regionIndex != before.regionIndex)
        {
            failure = "snapshot-region-does-not-match-loaded-region";
            return false;
        }

        const auto teleportThing = reinterpret_cast<TeleportThing>(
            gameInterface->vtable[kTeleportThingVtableIndex]);
        const auto* const creatureVtable = static_cast<void* const*>(
            before.creatureVtable);
        const auto modifyCombatHealth = reinterpret_cast<ModifyCombatHealth>(
            creatureVtable[kThingCreatureModifyCombatHealthVtableIndex]);
        const auto base = reinterpret_cast<std::uintptr_t>(g_gameModule);
        if (teleportThing != reinterpret_cast<TeleportThing>(
                base + kGameScriptInterfaceTeleportThingRva) ||
            modifyCombatHealth != reinterpret_cast<ModifyCombatHealth>(
                base + kThingPlayerCreatureModifyCombatHealthRva))
        {
            failure = "character-snapshot-native-seam-validation-failed";
            return false;
        }

        __try
        {
            // These final two arguments mirror the retail TeleportThing script
            // dispatcher's default call.  No map transition is requested.
            teleportThing(
                gameInterface,
                &hero,
                g_characterSnapshot.position,
                g_characterSnapshot.facingAngle,
                false,
                0);

            const float healthDelta =
                g_characterSnapshot.combatHealth - before.combatHealth;
            if (std::fabs(healthDelta) > 0.01f)
            {
                // Use CThingPlayerCreature's real virtual health mutation path,
                // including its normal clamping and gameplay side effects.
                modifyCombatHealth(before.creature, healthDelta, false);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            failure = "character-snapshot-native-call-raised-structured-exception";
            return false;
        }

        g_characterSnapshotApplied.store(true, std::memory_order_release);
        g_characterStateStableSamples.store(0, std::memory_order_release);
        char detail[512] = {};
        std::snprintf(
            detail,
            std::size(detail),
            "server_character_id=%s region_index=%d before_position=(%.6f,%.6f,%.6f) target_position=(%.6f,%.6f,%.6f) before_facing=%.6f target_facing=%.6f before_combat_health=%.3f target_combat_health=%.3f",
            g_characterSnapshot.serverCharacterId.c_str(),
            before.regionIndex,
            before.position[0],
            before.position[1],
            before.position[2],
            g_characterSnapshot.position[0],
            g_characterSnapshot.position[1],
            g_characterSnapshot.position[2],
            before.facingAngle,
            g_characterSnapshot.facingAngle,
            before.combatHealth,
            g_characterSnapshot.combatHealth);
        LogEvent("CharacterSnapshotApplied", detail);
        failure = nullptr;
        return true;
    }

    bool CharacterSnapshotMatches(
        const CharacterState& state,
        const char*& failure)
    {
        constexpr float kPositionTolerance = 1.5f;
        constexpr float kFacingTolerance = 0.02f;
        constexpr float kHealthTolerance = 0.01f;
        if (state.regionIndex != g_characterSnapshot.regionIndex)
        {
            failure = "server-character-region-does-not-match";
            return false;
        }
        if (std::fabs(state.position[0] - g_characterSnapshot.position[0]) >
                kPositionTolerance ||
            std::fabs(state.position[1] - g_characterSnapshot.position[1]) >
                kPositionTolerance ||
            std::fabs(state.position[2] - g_characterSnapshot.position[2]) >
                kPositionTolerance)
        {
            failure = "server-character-position-has-not-converged";
            return false;
        }
        if (std::fabs(state.facingAngle - g_characterSnapshot.facingAngle) >
            kFacingTolerance)
        {
            failure = "server-character-facing-has-not-converged";
            return false;
        }
        if (std::fabs(state.combatHealth - g_characterSnapshot.combatHealth) >
            kHealthTolerance)
        {
            failure = "server-character-combat-health-has-not-converged";
            return false;
        }
        failure = nullptr;
        return true;
    }

    void ObserveBootstrapHeroReadiness()
    {
        const bool bootstrapScenario = ScenarioIs(L"bootstrap_fixture_probe");
        const bool loadScenario = ScenarioLoadsFixture();
        const bool appearanceScenario = ScenarioIs(L"appearance_cycle");
        const bool startInvoked = bootstrapScenario
            ? g_bootstrapStartInvoked.load(std::memory_order_acquire)
            : loadScenario &&
                g_loadFixtureStartInvoked.load(std::memory_order_acquire);
        const bool observationComplete = appearanceScenario
            ? g_appearanceCycleScenario.IsComplete()
            : bootstrapScenario
            ? g_bootstrapHeroReadyLogged.load(std::memory_order_acquire)
            : loadScenario &&
                (g_characterSnapshotConfigured
                    ? g_characterSnapshotAssertionPassed.load(std::memory_order_acquire)
                    : g_characterStateAssertionPassed.load(std::memory_order_acquire));
        if ((!bootstrapScenario && !loadScenario) ||
            !startInvoked ||
            observationComplete)
        {
            return;
        }

        const ULONGLONG now = GetTickCount64();
        const ULONGLONG invokedAt = bootstrapScenario
            ? g_bootstrapStartInvokedAt.load(std::memory_order_acquire)
            : g_loadFixtureStartInvokedAt.load(std::memory_order_acquire);
        if (invokedAt == 0 || now - invokedAt < 5'000)
        {
            return;
        }
        ULONGLONG previousProbe =
            g_bootstrapHeroLastProbeAt.load(std::memory_order_acquire);
        if (now - previousProbe < 1'000 ||
            !g_bootstrapHeroLastProbeAt.compare_exchange_strong(
                previousProbe,
                now,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            return;
        }

        GameScriptInterface* gameInterface = nullptr;
        if (!ResolveGameInterface(gameInterface))
        {
            return;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(g_gameModule);
        const auto constructString = reinterpret_cast<CharStringConstructor>(
            base + kCharStringConstructorRva);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + kCharStringDestructorRva);
        const auto getThingWithScriptName = reinterpret_cast<GetThingWithScriptName>(
            gameInterface->vtable[kGetThingWithScriptNameVtableIndex]);
        const auto isNull = reinterpret_cast<ScriptThingIsNull>(
            base + kScriptThingIsNullRva);
        auto* const expectedScriptThingVtable = reinterpret_cast<void**>(
            base + kScriptThingVtableRva);

        constexpr char kHeroScriptName[] = "SCRIPT_NAME_HERO";
        CharString heroScriptName = {};
        ScriptThing hero = {};
        bool heroScriptNameConstructed = false;
        bool heroReady = false;
        bool characterStateReady = false;
        bool snapshotAppliedThisProbe = false;
        bool snapshotApplicationFailed = false;
        CharacterState characterState = {};
        const char* characterStateFailure = nullptr;
        void* heroImplementation = nullptr;
        CountedPointerInfo* heroPointerInfo = nullptr;
        const unsigned int probe =
            g_bootstrapHeroProbeCount.fetch_add(1, std::memory_order_acq_rel) + 1;

        __try
        {
            constructString(&heroScriptName, kHeroScriptName, -1);
            heroScriptNameConstructed = true;
            ScriptThing* const returned = getThingWithScriptName(
                gameInterface,
                &hero,
                &heroScriptName);
            heroImplementation = hero.implementation;
            heroPointerInfo = hero.pointerInfo;
            heroReady = returned == &hero &&
                hero.vtable == expectedScriptThingVtable &&
                !isNull(&hero);

            if (loadScenario && heroReady)
            {
                characterStateReady = ReadCharacterState(
                    gameInterface,
                    hero,
                    characterState,
                    characterStateFailure);
                if (characterStateReady &&
                    g_characterSnapshotConfigured &&
                    !g_characterSnapshotApplied.load(std::memory_order_acquire) &&
                    CharacterSnapshotBaselineIsStable(characterState))
                {
                    snapshotAppliedThisProbe = ApplyCharacterSnapshot(
                        gameInterface,
                        hero,
                        characterState,
                        characterStateFailure);
                    snapshotApplicationFailed = !snapshotAppliedThisProbe;
                    if (snapshotApplicationFailed)
                    {
                        LogEvent(
                            "ClientFailed",
                            characterStateFailure != nullptr
                                ? characterStateFailure
                                : "unknown-character-snapshot-application-failure");
                    }
                }
            }

            if (probe <= 3 || heroReady)
            {
                char detail[256] = {};
                std::snprintf(
                    detail,
                    std::size(detail),
                    "probe=%u returned=%p result=%p vtable=%p implementation=%p pointer_info=%p ready=%s",
                    probe,
                    returned,
                    &hero,
                    hero.vtable,
                    heroImplementation,
                    heroPointerInfo,
                    heroReady ? "true" : "false");
                LogEvent(
                    bootstrapScenario ? "BootstrapHeroProbe" : "FixtureLoadHeroProbe",
                    detail);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            LogEvent("ClientFailed", "world-Hero-lookup-raised-structured-exception");
        }

        if (appearanceScenario && heroReady && characterStateReady &&
            g_characterStateAssertionPassed.load(std::memory_order_acquire))
        {
            const AppearanceCycleCharacterSnapshot appearanceCharacter = {
                characterState.progressionHealthValue,
                characterState.combatHealth,
                characterState.combatHealthMaximum,
                characterState.regionIndex,
                characterState.creature,
                characterState.creatureVtable,
            };
            g_appearanceCycleScenario.Tick(appearanceCharacter);
        }

        if (hero.vtable == expectedScriptThingVtable)
        {
            RetainTransformHandle(hero, "world Hero observation");
        }
        if (heroScriptNameConstructed)
        {
            __try
            {
                destroyString(&heroScriptName);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                LogEvent("ClientFailed", "world-Hero-script-name-cleanup-failed");
                return;
            }
        }

        if (heroReady &&
            !g_bootstrapHeroReadyLogged.exchange(true, std::memory_order_acq_rel))
        {
            char detail[192] = {};
            std::snprintf(
                detail,
                std::size(detail),
                "implementation=%p pointer_info=%p script_name=SCRIPT_NAME_HERO",
                heroImplementation,
                heroPointerInfo);
            LogEvent("HeroReady", detail);
            LogEvent(
                "WorldReady",
                bootstrapScenario
                    ? "Hero object resolved after isolated New Game bootstrap"
                    : "Hero object resolved after exact isolated AutoSave load");
            g_scriptHost.DispatchWorldReady();
        }

        if (loadScenario && heroReady)
        {
            if (snapshotApplicationFailed || snapshotAppliedThisProbe)
            {
                return;
            }
            if (!characterStateReady)
            {
                if (probe <= 3)
                {
                    LogEvent(
                        "CharacterStateUnavailable",
                        characterStateFailure != nullptr
                            ? characterStateFailure
                            : "unknown");
                }
                return;
            }
            if (g_characterSnapshotConfigured &&
                !g_characterSnapshotApplied.load(std::memory_order_acquire))
            {
                return;
            }

            if (g_characterSnapshotConfigured)
            {
                const unsigned int attempt =
                    g_characterSnapshotVerificationAttempts.fetch_add(
                        1,
                        std::memory_order_acq_rel) + 1;
                const char* snapshotFailure = nullptr;
                if (!CharacterSnapshotMatches(characterState, snapshotFailure))
                {
                    g_characterStateStableSamples.store(0, std::memory_order_release);
                    if (attempt <= 5)
                    {
                        char detail[384] = {};
                        std::snprintf(
                            detail,
                            std::size(detail),
                            "attempt=%u reason=%s observed_position=(%.6f,%.6f,%.6f) observed_facing=%.6f observed_combat_health=%.3f",
                            attempt,
                            snapshotFailure != nullptr ? snapshotFailure : "unknown",
                            characterState.position[0],
                            characterState.position[1],
                            characterState.position[2],
                            characterState.facingAngle,
                            characterState.combatHealth);
                        LogEvent("CharacterSnapshotVerificationPending", detail);
                    }
                    return;
                }
            }

            const unsigned int sample =
                g_characterStateStableSamples.fetch_add(
                    1,
                    std::memory_order_acq_rel) + 1;
            char detail[384] = {};
            std::snprintf(
                detail,
                std::size(detail),
                "sample=%u region_index=%d position=(%.6f,%.6f,%.6f) facing=%.6f combat_health=%.3f combat_health_maximum=%.3f progression_health=%d progression_health_maximum=%d creature=%p creature_vtable=%p",
                sample,
                characterState.regionIndex,
                characterState.position[0],
                characterState.position[1],
                characterState.position[2],
                characterState.facingAngle,
                characterState.combatHealth,
                characterState.combatHealthMaximum,
                characterState.progressionHealthValue,
                characterState.progressionHealthMaximum,
                characterState.creature,
                characterState.creatureVtable);
            LogEvent("CharacterStateSample", detail);
            if (g_characterSnapshotConfigured)
            {
                LogEvent("CharacterSnapshotSample", detail);
            }

            if (sample >= 3)
            {
                if (!g_characterStateAssertionPassed.exchange(
                        true,
                        std::memory_order_acq_rel))
                {
                    LogEvent(
                        "AssertionPassed",
                        g_characterSnapshotConfigured
                            ? "three consecutive server-character target transform and combat-health samples"
                            : "three consecutive finite Hero transform, combat-health, progression, and active-creature samples");
                }
                if (g_characterSnapshotConfigured &&
                    !g_characterSnapshotAssertionPassed.exchange(
                        true,
                        std::memory_order_acq_rel))
                {
                    char snapshotDetail[256] = {};
                    std::snprintf(
                        snapshotDetail,
                        std::size(snapshotDetail),
                        "server_character_id=%s display_name=%s samples=%u",
                        g_characterSnapshot.serverCharacterId.c_str(),
                        g_characterSnapshot.displayName.c_str(),
                        sample);
                    LogEvent("CharacterSnapshotAssertionPassed", snapshotDetail);
                }
            }
        }
    }

    void DriveSaveListObservation()
    {
        if ((!ScenarioIs(L"observe_save_list") && !ScenarioLoadsFixture()) ||
            !g_frontendReadyLogged.load(std::memory_order_acquire) ||
            g_saveListRequested.load(std::memory_order_acquire))
        {
            return;
        }

        const ULONGLONG readyAt = g_frontendReadyAt.load(std::memory_order_acquire);
        if (readyAt == 0 || GetTickCount64() - readyAt < 1'500)
        {
            return;
        }
        if (g_saveListBeginAttempted.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }

        void* const object = g_loadGamePageObject.load(std::memory_order_acquire);
        if (object == nullptr)
        {
            LogEvent("ClientFailed", "load-game-page-object-unavailable");
            return;
        }

        __try
        {
            void* const expectedVtable = reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(g_gameModule) + kLoadGamePageVtableRva);
            void* const expectedDoBegin = reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(g_gameModule) + kLoadGamePageDoBeginRva);
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
            g_saveListRequested.store(true, std::memory_order_release);
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
        std::uint32_t* exactAutoSaveIdentity)
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

            const auto base = reinterpret_cast<std::uintptr_t>(g_gameModule);
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

            unsigned int autoSaveMatches = 0;
            std::uint32_t matchedAutoSaveIdentity = 0xFFFFFFFFu;
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
                if (valid && nameReadable && std::wcscmp(name, L"AutoSave") == 0)
                {
                    ++autoSaveMatches;
                    matchedAutoSaveIdentity = identity;
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

            if (exactAutoSaveIdentity != nullptr)
            {
                if (autoSaveMatches != 1)
                {
                    char detail[128] = {};
                    std::snprintf(
                        detail,
                        std::size(detail),
                        "exact_name=AutoSave valid_matches=%u",
                        autoSaveMatches);
                    LogEvent("SaveEntrySelectionRejected", detail);
                    return false;
                }
                *exactAutoSaveIdentity = matchedAutoSaveIdentity;
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
        if (!g_saveListRequested.load(std::memory_order_acquire) ||
            g_saveListReadyLogged.load(std::memory_order_acquire))
        {
            return;
        }

        void* const object = g_loadGamePageObject.load(std::memory_order_acquire);
        if (object == nullptr)
        {
            return;
        }

        __try
        {
            const auto* const bytes = static_cast<const std::uint8_t*>(object);
            const ULONGLONG now = GetTickCount64();
            ULONGLONG previousTick =
                g_saveListLastTickAt.load(std::memory_order_acquire);
            if (now - previousTick < 16 ||
                !g_saveListLastTickAt.compare_exchange_strong(
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
                reinterpret_cast<std::uintptr_t>(g_gameModule) + kLoadGamePageDoTickRva);
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
                g_saveListTickCount.fetch_add(1, std::memory_order_acq_rel) + 1;
            const unsigned int previousPhase =
                g_saveListLastLoggedPhase.exchange(phase, std::memory_order_acq_rel);
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
                !g_saveListReadyLogged.exchange(true, std::memory_order_acq_rel))
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
                std::uint32_t autoSaveIdentity = 0xFFFFFFFFu;
                if (!ObserveLocalSaveEntries(
                        object,
                        ScenarioLoadsFixture() ? &autoSaveIdentity : nullptr))
                {
                    if (ScenarioLoadsFixture())
                    {
                        LogEvent("ClientFailed", "fixture AutoSave identity could not be resolved exactly");
                    }
                    return;
                }
                if (ScenarioLoadsFixture())
                {
                    *reinterpret_cast<std::uint32_t*>(
                        static_cast<std::uint8_t*>(object) + 0x74) = autoSaveIdentity;
                    g_loadFixtureAutoSaveIdentity.store(
                        autoSaveIdentity,
                        std::memory_order_release);
                    g_loadFixtureAutoSaveSelectedAt.store(
                        GetTickCount64(),
                        std::memory_order_release);
                    g_loadFixtureAutoSaveSelected.store(
                        true,
                        std::memory_order_release);
                    char selected[160] = {};
                    std::snprintf(
                        selected,
                        std::size(selected),
                        "exact_name=AutoSave identity=%lu selection_field=%lu",
                        static_cast<unsigned long>(autoSaveIdentity),
                        static_cast<unsigned long>(
                            *reinterpret_cast<const std::uint32_t*>(bytes + 0x74)));
                    LogEvent("FixtureAutoSaveSelected", selected);
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
            !g_loadFixtureAutoSaveSelected.load(std::memory_order_acquire) ||
            g_bootstrapHeroReadyLogged.load(std::memory_order_acquire) ||
            g_loadFixtureMainMenuReleased.load(std::memory_order_acquire))
        {
            return;
        }

        const ULONGLONG selectedAt =
            g_loadFixtureAutoSaveSelectedAt.load(std::memory_order_acquire);
        if (selectedAt == 0 || GetTickCount64() - selectedAt < 250)
        {
            return;
        }

        void* const object = g_frontEndMainMenuObject.load(std::memory_order_acquire);
        if (object == nullptr)
        {
            if (g_loadFixtureStartInvoked.load(std::memory_order_acquire))
            {
                g_loadFixtureMainMenuReleased.store(true, std::memory_order_release);
                LogEvent(
                    "FixtureMainMenuReleased",
                    "front-end main-menu object cleared after Continue transition");
                return;
            }
            LogEvent("ClientFailed", "front-end main menu disappeared before fixture load");
            g_loadFixtureStartInvoked.store(true, std::memory_order_release);
            return;
        }

        __try
        {
            auto* const bytes = static_cast<std::uint8_t*>(object);
            void* const vtable = *reinterpret_cast<void* const*>(object);
            void* const expectedVtable = reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(g_gameModule) +
                kFrontEndMainMenuVtableRva);
            if (vtable != expectedVtable)
            {
                if (g_loadFixtureStartInvoked.load(std::memory_order_acquire))
                {
                    g_loadFixtureMainMenuReleased.store(
                        true,
                        std::memory_order_release);
                    LogEvent(
                        "FixtureMainMenuReleased",
                        "front-end main-menu object retired after Continue transition");
                    return;
                }
                LogEvent("ClientFailed", "fixture-load main-menu vtable validation failed");
                g_loadFixtureStartInvoked.store(true, std::memory_order_release);
                return;
            }

            const std::uint32_t expectedIdentity =
                g_loadFixtureAutoSaveIdentity.load(std::memory_order_acquire);
            const std::uint32_t flags =
                *reinterpret_cast<const std::uint32_t*>(bytes + 0x58);
            const ULONGLONG now = GetTickCount64();

            ULONGLONG previousTick =
                g_bootstrapMainMenuLastTickAt.load(std::memory_order_acquire);
            if (now - previousTick < 16 ||
                !g_bootstrapMainMenuLastTickAt.compare_exchange_strong(
                    previousTick,
                    now,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                return;
            }

            if (!g_loadFixtureStartInvoked.load(std::memory_order_acquire) &&
                (flags & 4u) != 0)
            {
                const std::uint32_t menuState =
                    *reinterpret_cast<const std::uint32_t*>(bytes + 0x7C);
                if ((menuState & 1u) != 0)
                {
                    LogEvent(
                        "ClientFailed",
                        "main menu reports New Game instead of a valid Continue target");
                    g_loadFixtureStartInvoked.store(true, std::memory_order_release);
                    return;
                }

                void* const implementation =
                    reinterpret_cast<void* const*>(vtable)[81];
                void* const expectedImplementation = reinterpret_cast<void*>(
                    reinterpret_cast<std::uintptr_t>(g_gameModule) +
                    kFrontEndMainMenuDoOnUIEventRva);
                if (implementation != expectedImplementation)
                {
                    LogEvent(
                        "ClientFailed",
                        "fixture-load main-menu OnUIEvent validation failed");
                    g_loadFixtureStartInvoked.store(true, std::memory_order_release);
                    return;
                }

                const std::uint32_t previousIdentity =
                    *reinterpret_cast<const std::uint32_t*>(bytes + 0x78);
                *reinterpret_cast<std::uint32_t*>(bytes + 0x78) =
                    expectedIdentity;
                g_loadFixtureStartInvoked.store(true, std::memory_order_release);
                g_loadFixtureStartInvokedAt.store(now, std::memory_order_release);
                char detail[256] = {};
                std::snprintf(
                    detail,
                    std::size(detail),
                    "exact_name=AutoSave identity=%lu previous_continue_identity=%lu menu_state=0x%08lX implementation=%p",
                    static_cast<unsigned long>(expectedIdentity),
                    static_cast<unsigned long>(previousIdentity),
                    static_cast<unsigned long>(menuState),
                    implementation);
                LogEvent("FixtureContinueInvoked", detail);
                using MainMenuDoOnUIEvent = void(__thiscall*)(void*, int, int);
                reinterpret_cast<MainMenuDoOnUIEvent>(implementation)(object, 17, 0);
                LogEvent(
                    "FixtureContinueRequested",
                    "validated main-menu Continue event returned for exact AutoSave identity");
                return;
            }

            void* const tickImplementation =
                reinterpret_cast<void* const*>(vtable)[80];
            void* const expectedTickImplementation = reinterpret_cast<void*>(
                reinterpret_cast<std::uintptr_t>(g_gameModule) +
                kFrontEndMainMenuDoTickRva);
            if (tickImplementation != expectedTickImplementation)
            {
                LogEvent("ClientFailed", "fixture-load main-menu DoTick validation failed");
                g_loadFixtureStartInvoked.store(true, std::memory_order_release);
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
                g_bootstrapMainMenuTickCount.fetch_add(
                    1,
                    std::memory_order_acq_rel) + 1;
            const unsigned int previousPhase =
                g_bootstrapMainMenuLastLoggedPhase.exchange(
                    updatedPhase,
                    std::memory_order_acq_rel);
            if (tick <= 3 || previousPhase != updatedPhase ||
                (!g_loadFixtureStartInvoked.load(std::memory_order_acquire) &&
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
                    g_loadFixtureStartInvoked.load(std::memory_order_acquire)
                        ? "true"
                        : "false");
                LogEvent("FixtureMainMenuTickProgress", detail);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_loadFixtureStartInvoked.store(true, std::memory_order_release);
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
        const HANDLE shutdownEvent = g_runtimeConfiguration.ShutdownEvent();
        if (shutdownEvent != nullptr &&
            WaitForSingleObject(shutdownEvent, 0) == WAIT_OBJECT_0 &&
            !g_automationShutdownStarted.exchange(true, std::memory_order_acq_rel))
        {
            Log("Automation: run-scoped shutdown event received; posting WM_QUIT.");
            LogEvent("ShutdownStarted", "run-scoped-event");
            PostQuitMessage(0);
            // The Win32 launcher layer consumes WM_QUIT without unwinding
            // the UE3 process at the front end. This path exists only for
            // a run-scoped automation process owned by our launcher.
            ExitProcess(ERROR_SUCCESS);
        }

        for (auto& candidate : g_frontEndStartObjects)
        {
            TryPassFrontEndStart(
                candidate.load(std::memory_order_acquire),
                nullptr,
                "drained game-thread message queue");
            if (g_automationPassStartStage.load(std::memory_order_acquire) != 0)
            {
                break;
            }
        }
        DriveBootstrapFixtureProbe();
        ObserveBootstrapHeroReadiness();
        DriveSaveListObservation();
        ObserveSaveListReadiness();
        DriveFixtureLoad();
    }

    bool ValidateExecutable()
    {
        g_gameModule = GetModuleHandleW(nullptr);
        if (g_gameModule == nullptr)
        {
            Log("Target validation failed: the main executable module is unavailable.");
            return false;
        }

        const auto* base = reinterpret_cast<const std::uint8_t*>(g_gameModule);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        {
            Log("Target validation failed: invalid DOS header.");
            return false;
        }

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
            nt->FileHeader.TimeDateStamp != kExpectedTimestamp ||
            nt->OptionalHeader.SizeOfImage != kExpectedImageSize ||
            nt->OptionalHeader.AddressOfEntryPoint != kExpectedEntryPointRva)
        {
            Log("Target validation failed: this is not the analyzed Fable Anniversary executable.");
            return false;
        }

        constexpr std::array<std::uint8_t, 9> kCharStringConstructorPrefix = {
            0x56, 0x8B, 0xF1, 0xC7, 0x06, 0x00, 0x00, 0x00, 0x00,
        };
        constexpr std::array<std::uint8_t, 2> kGetHeroPrefix = {0x8B, 0x0D};
        // Skip the relocated absolute operand at +2 when validating an ASLR image.
        constexpr std::array<std::uint8_t, 10> kGetHeroBody = {
            0x8B, 0x81, 0xE4, 0x00, 0x00, 0x00, 0x8B, 0x50, 0x08, 0x52,
        };
        constexpr std::array<std::uint8_t, 3> kGetThingWithScriptNamePrefix = {
            0x6A, 0xFF, 0x68,
        };
        constexpr std::array<std::uint8_t, 3> kTurnCreatureIntoPrefix = {0x6A, 0xFF, 0x68};
        constexpr std::array<std::uint8_t, 4> kScriptThingDestructorPrefix = {
            0x56, 0x8B, 0xF1, 0xE8,
        };
        constexpr std::array<std::uint8_t, 5> kScriptThingIsNullPrefix = {
            0x8B, 0x49, 0x04, 0x85, 0xC9,
        };
        constexpr std::array<std::uint8_t, 6> kScriptThingStateAccessorPrefix = {
            0x8B, 0x49, 0x04, 0x85, 0xC9, 0x75,
        };
        constexpr std::array<std::uint8_t, 7> kActiveHeroSelectorPrefix = {
            0x8B, 0x41, 0x10, 0x53, 0x55, 0x56, 0x57,
        };
        constexpr std::array<std::uint8_t, 4> kActiveHeroCreatureResolverPrefix = {
            0x83, 0xC1, 0x2C, 0xE9,
        };
        constexpr std::array<std::uint8_t, 13> kHealthMaximumBody = {
            0x8B, 0x80, 0x00, 0x01, 0x00, 0x00, 0x8B,
            0x80, 0xE4, 0x00, 0x00, 0x00, 0xC3,
        };
        constexpr std::array<std::uint8_t, 8> kRegionManagerResolveIndexPrefix = {
            0x53, 0x55, 0x56, 0x8B, 0xF1, 0x8B, 0x4E, 0x38,
        };

        if (!BytesMatch(base + kCharStringConstructorRva, kCharStringConstructorPrefix) ||
            !BytesMatch(base + kGetHeroRva, kGetHeroPrefix) ||
            !BytesMatch(base + kGetHeroRva + 6, kGetHeroBody) ||
            !BytesMatch(base + kGetThingWithScriptNameRva, kGetThingWithScriptNamePrefix) ||
            !BytesMatch(base + kTurnCreatureIntoRva, kTurnCreatureIntoPrefix) ||
            !BytesMatch(base + kScriptThingDestructorRva, kScriptThingDestructorPrefix) ||
            !BytesMatch(base + kScriptThingIsNullRva, kScriptThingIsNullPrefix) ||
            !BytesMatch(
                base + kScriptThingGetPositionVectorRva,
                kScriptThingStateAccessorPrefix) ||
            !BytesMatch(
                base + kScriptThingGetFacingAngleRva,
                kScriptThingStateAccessorPrefix) ||
            !BytesMatch(base + kActiveHeroSelectorRva, kActiveHeroSelectorPrefix) ||
            !BytesMatch(
                base + kActiveHeroCreatureResolverRva,
                kActiveHeroCreatureResolverPrefix) ||
            !BytesMatch(
                base + kHeroProgressionHealthGetMaximumRva + 5,
                kHealthMaximumBody) ||
            !BytesMatch(base + kRegionManagerResolveIndexRva, kRegionManagerResolveIndexPrefix))
        {
            Log("Target validation failed: one or more native signatures drifted.");
            return false;
        }

        const auto scriptThingVtable = reinterpret_cast<void* const*>(
            base + kScriptThingVtableRva);
        if (scriptThingVtable[kScriptThingDestructorVtableIndex] !=
                reinterpret_cast<const void*>(base + kScriptThingDestructorRva) ||
            scriptThingVtable[kScriptThingIsNullVtableIndex] !=
                reinterpret_cast<const void*>(base + kScriptThingIsNullRva))
        {
            Log("Target validation failed: the CScriptThing vtable layout drifted.");
            return false;
        }

        const auto thingPlayerCreatureVtable = reinterpret_cast<void* const*>(
            base + kThingPlayerCreatureVtableRva);
        if (thingPlayerCreatureVtable[kThingCreatureModifyCombatHealthVtableIndex] !=
            reinterpret_cast<const void*>(
                base + kThingPlayerCreatureModifyCombatHealthRva))
        {
            Log("Target validation failed: the CThingPlayerCreature combat-health vtable layout drifted.");
            return false;
        }

        Log("Target executable and transformation signatures validated.");
        return true;
    }

    bool ResolveGameInterface(GameScriptInterface*& gameInterface)
    {
        gameInterface = nullptr;
        if (g_gameModule == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(g_gameModule);
        __try
        {
            const auto interfaceSlot = reinterpret_cast<GameScriptInterface**>(
                base + kGameScriptInterfaceSlotRva);
            gameInterface = *interfaceSlot;
            if (gameInterface == nullptr || gameInterface->vtable == nullptr)
            {
                return false;
            }

            if (gameInterface->vtable != reinterpret_cast<void**>(base + kGameScriptInterfaceVtableRva) ||
                gameInterface->vtable[kGetHeroVtableIndex] !=
                    reinterpret_cast<void*>(base + kGetHeroRva) ||
                gameInterface->vtable[kGetThingWithScriptNameVtableIndex] !=
                    reinterpret_cast<void*>(base + kGetThingWithScriptNameRva) ||
                gameInterface->vtable[kTurnCreatureIntoVtableIndex] !=
                    reinterpret_cast<void*>(base + kTurnCreatureIntoRva) ||
                gameInterface->vtable[kTeleportThingVtableIndex] !=
                    reinterpret_cast<void*>(base + kGameScriptInterfaceTeleportThingRva))
            {
                gameInterface = nullptr;
                return false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            gameInterface = nullptr;
            return false;
        }

        return true;
    }

    bool RetainTransformHandle(ScriptThing& thing, const char* label)
    {
        if (thing.vtable == nullptr || g_retainedTransformHandleCount >= g_retainedTransformHandles.size())
        {
            return false;
        }

        unsigned int referenceCount = 0;
        bool referenceCountAvailable = false;
        __try
        {
            if (thing.pointerInfo != nullptr)
            {
                referenceCount = thing.pointerInfo->referenceCount;
                referenceCountAvailable = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            referenceCountAvailable = false;
        }

        g_retainedTransformHandles[g_retainedTransformHandleCount] = thing;
        ++g_retainedTransformHandleCount;
        thing = {};

        if (referenceCountAvailable)
        {
            LogFormat(
                "Native: retained %s handle for process lifetime; slot=%zu/64 ref_count=%u.",
                label,
                g_retainedTransformHandleCount,
                referenceCount);
        }
        else
        {
            LogFormat(
                "Native: retained %s handle for process lifetime; slot=%zu/64 ref_count=<unavailable>.",
                label,
                g_retainedTransformHandleCount);
        }
        return true;
    }

    TransformResult TransformHero(const char* creatureDefinition)
    {
        if (g_retainedTransformHandleCount + 2 > g_retainedTransformHandles.size())
        {
            Log("Native: transformation disabled because the diagnostic handle-retention limit was reached; restart the game.");
            return TransformResult::RetentionLimit;
        }

        GameScriptInterface* gameInterface = nullptr;
        if (!ResolveGameInterface(gameInterface))
        {
            return TransformResult::GameNotReady;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(g_gameModule);
        const auto constructString = reinterpret_cast<CharStringConstructor>(
            base + kCharStringConstructorRva);
        const auto destroyString = reinterpret_cast<CharStringDestructor>(
            base + kCharStringDestructorRva);
        const auto getThingWithScriptName = reinterpret_cast<GetThingWithScriptName>(
            gameInterface->vtable[kGetThingWithScriptNameVtableIndex]);
        const auto turnCreatureInto = reinterpret_cast<TurnCreatureInto>(
            gameInterface->vtable[kTurnCreatureIntoVtableIndex]);
        const auto isNull = reinterpret_cast<ScriptThingIsNull>(
            base + kScriptThingIsNullRva);
        auto* const expectedScriptThingVtable = reinterpret_cast<void**>(
            base + kScriptThingVtableRva);

        constexpr char kHeroScriptName[] = "SCRIPT_NAME_HERO";
        CharString heroScriptName = {};
        CharString definition = {};
        ScriptThing hero = {};
        ScriptThing result = {};
        bool heroScriptNameConstructed = false;
        bool definitionConstructed = false;
        TransformResult transformResult = TransformResult::StructuredException;
        NativeFault fault = {};
        const char* stage = "hero script-name constructor";

        LogFormat(
            "Native: transformation begin definition=%s thread=%lu interface=%p acquisition=SCRIPT_NAME_HERO.",
            creatureDefinition,
            static_cast<unsigned long>(GetCurrentThreadId()),
            gameInterface);

        __try
        {
            stage = "hero script-name constructor";
            constructString(&heroScriptName, kHeroScriptName, -1);
            heroScriptNameConstructed = true;

            stage = "GetThingWithScriptName(SCRIPT_NAME_HERO)";
            ScriptThing* const returnedHero = getThingWithScriptName(
                gameInterface,
                &hero,
                &heroScriptName);

            stage = "direct hero lookup result inspection";
            LogFormat(
                "Native: direct hero lookup returned=%p result=%p vtable=%p implementation=%p pointer_info=%p expected_vtable=%p.",
                returnedHero,
                &hero,
                hero.vtable,
                hero.implementation,
                hero.pointerInfo,
                expectedScriptThingVtable);

            if (returnedHero != &hero || hero.vtable != expectedScriptThingVtable)
            {
                transformResult = TransformResult::InvalidInterface;
            }
            else
            {
                stage = "direct hero CScriptThing::IsNull preflight";
                if (isNull(&hero))
                {
                    transformResult = TransformResult::HeroUnavailable;
                }
                else
                {
                    stage = "creature-definition CCharString constructor";
                    constructString(&definition, creatureDefinition, -1);
                    definitionConstructed = true;
                    LogFormat(
                        "Native: creature definition string constructed; storage=%p.",
                        definition.stringData);

                    stage = "TurnCreatureInto";
                    ScriptThing* const returned = turnCreatureInto(
                        gameInterface,
                        &result,
                        &hero,
                        &definition);

                    stage = "TurnCreatureInto result inspection";
                    LogFormat(
                        "Native: TurnCreatureInto returned=%p result=%p vtable=%p implementation=%p pointer_info=%p.",
                        returned,
                        &result,
                        result.vtable,
                        result.implementation,
                        result.pointerInfo);

                    if (returned != &result || result.vtable != expectedScriptThingVtable)
                    {
                        transformResult = TransformResult::EngineRejected;
                    }
                    else
                    {
                        stage = "result CScriptThing::IsNull";
                        transformResult = isNull(&result)
                            ? TransformResult::EngineRejected
                            : TransformResult::Succeeded;
                    }
                }
            }
        }
        __except (CaptureNativeFault(GetExceptionInformation(), &fault, stage))
        {
            transformResult = TransformResult::StructuredException;
        }

        if (fault.code != ERROR_SUCCESS)
        {
            LogNativeFault(fault);
        }

        if (definitionConstructed)
        {
            NativeFault cleanupFault = {};
            stage = "CCharString destructor";
            __try
            {
                destroyString(&definition);
            }
            __except (CaptureNativeFault(GetExceptionInformation(), &cleanupFault, stage))
            {
                transformResult = TransformResult::StructuredException;
            }
            if (cleanupFault.code != ERROR_SUCCESS)
            {
                LogNativeFault(cleanupFault);
            }
        }

        if (heroScriptNameConstructed)
        {
            NativeFault cleanupFault = {};
            stage = "hero script-name CCharString destructor";
            __try
            {
                destroyString(&heroScriptName);
            }
            __except (CaptureNativeFault(GetExceptionInformation(), &cleanupFault, stage))
            {
                transformResult = TransformResult::StructuredException;
            }
            if (cleanupFault.code != ERROR_SUCCESS)
            {
                LogNativeFault(cleanupFault);
            }
        }

        if (result.vtable == expectedScriptThingVtable)
        {
            RetainTransformHandle(result, "TurnCreatureInto result");
        }
        else if (result.vtable != nullptr)
        {
            LogFormat(
                "Native: skipping result cleanup because vtable=%p did not match expected=%p.",
                result.vtable,
                expectedScriptThingVtable);
        }

        if (hero.vtable == expectedScriptThingVtable)
        {
            RetainTransformHandle(hero, "direct hero lookup");
        }
        else if (hero.vtable != nullptr)
        {
            LogFormat(
                "Native: skipping direct hero cleanup because vtable=%p did not match expected=%p.",
                hero.vtable,
                expectedScriptThingVtable);
        }

        return transformResult;
    }

    const char* DescribeResult(TransformResult result)
    {
        switch (result)
        {
        case TransformResult::Succeeded:
            return "succeeded";
        case TransformResult::GameNotReady:
            return "game interface is not ready";
        case TransformResult::InvalidInterface:
            return "game interface validation failed";
        case TransformResult::HeroUnavailable:
            return "hero is unavailable, dead, or between regions";
        case TransformResult::EngineRejected:
            return "engine rejected the creature definition";
        case TransformResult::RetentionLimit:
            return "was blocked by the diagnostic handle-retention limit; restart the game";
        case TransformResult::StructuredException:
            return "engine call raised a structured exception";
        default:
            return "unknown result";
        }
    }

    void CycleCreature()
    {
        const std::size_t nextIndex = (g_creatureIndex.load() + 1) % kCreatureCycle.size();
        const char* const creature = kCreatureCycle[nextIndex];

        char message[512] = {};
        std::snprintf(
            message,
            std::size(message),
            "Key 1: attempting form %zu/%zu: %s",
            nextIndex + 1,
            kCreatureCycle.size(),
            creature);
        Log(message);

        const TransformResult result = TransformHero(creature);
        std::snprintf(
            message,
            std::size(message),
            "Transformation to %s %s.",
            creature,
            DescribeResult(result));
        Log(message);

        if (result == TransformResult::Succeeded)
        {
            g_creatureIndex.store(nextIndex);
        }
    }

    void RestoreHero()
    {
        Log("Shift+1: attempting emergency restore to CREATURE_HERO.");
        const TransformResult result = TransformHero(kCreatureCycle[0]);

        char message[256] = {};
        std::snprintf(
            message,
            std::size(message),
            "Emergency restore to CREATURE_HERO %s.",
            DescribeResult(result));
        Log(message);

        if (result == TransformResult::Succeeded)
        {
            g_creatureIndex.store(0);
        }
    }

    void QueueOneKeyPress(const char* source, bool shiftPressed)
    {
        LogFormat(
            "Input: number-row 1 pressed via %s; shift=%s.",
            source,
            shiftPressed ? "true" : "false");
        PendingTransform expected = PendingTransform::None;
        const PendingTransform requested = shiftPressed
            ? PendingTransform::Restore
            : PendingTransform::Cycle;
        if (!g_pendingTransform.compare_exchange_strong(
                expected,
                requested,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            Log("Input: transformation request ignored because one is already queued.");
            return;
        }

        Log(g_runtimeConfiguration.Mode() == ClientMode::AppearanceCycle
            ? "Input: appearance change queued on the next game-window timer dispatch."
            : "Input: transformation queued for direct hero lookup on the next game-window timer dispatch.");
    }

    void ExecutePendingTransform()
    {
        const PendingTransform pending = g_pendingTransform.exchange(
            PendingTransform::None,
            std::memory_order_acq_rel);
        if (pending == PendingTransform::None)
        {
            return;
        }

        LogFormat(
            "Event: executing queued %s on game-window timer thread=%lu mode=%s.",
            pending == PendingTransform::Restore ? "restore" : "cycle",
            static_cast<unsigned long>(GetCurrentThreadId()),
            g_runtimeConfiguration.Mode() == ClientMode::AppearanceCycle
                ? "angelscript_puppet"
                : "transform_probe");
        if (g_runtimeConfiguration.Mode() == ClientMode::AppearanceCycle)
        {
            g_scriptHost.DispatchKeyPressed(
                '1',
                pending == PendingTransform::Restore);
        }
        else
        {
            if (pending == PendingTransform::Restore)
            {
                RestoreHero();
            }
            else
            {
                CycleCreature();
            }
        }
    }

    void ObserveOneKeyState(bool isDown, bool shiftPressed, const char* source)
    {
        if (isDown && !g_oneKeyIsDown)
        {
            QueueOneKeyPress(source, shiftPressed);
        }
        g_oneKeyIsDown = isDown;
    }

    void ProbeGameInterfaceAvailability()
    {
        if (++g_interfaceProbeTicks < 60)
        {
            return;
        }
        g_interfaceProbeTicks = 0;

        const int steamApiReadyState = GetModuleHandleW(L"steam_api.dll") == nullptr ? 0 : 1;
        if (steamApiReadyState != g_lastSteamApiReadyState)
        {
            LogFormat(
                "Startup: steam_api changed to %s.",
                steamApiReadyState != 0 ? "loaded" : "not-loaded");
            g_lastSteamApiReadyState = steamApiReadyState;
        }

        GameScriptInterface* gameInterface = nullptr;
        const int readyState = ResolveGameInterface(gameInterface) ? 1 : 0;
        if (readyState != g_lastInterfaceReadyState)
        {
            LogFormat(
                "Startup: game-script interface changed to %s (interface=%p).",
                readyState != 0 ? "ready" : "not-ready",
                gameInterface);
            g_lastInterfaceReadyState = readyState;
            LogEvent(
                readyState != 0 ? "GameScriptInterfaceReady" : "GameScriptInterfaceUnavailable",
                readyState != 0 ? "validated" : "not-ready");
        }
    }

    void PollHotkey()
    {
        if (!g_firstTimerTickLogged)
        {
            Log("Event: first game-window timer tick received; polling fallback is alive.");
            g_firstTimerTickLogged = true;
        }
        ProbeGameInterfaceAvailability();

        const bool reloadIsDown = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
        if (reloadIsDown && !g_reloadKeyIsDown)
        {
            Log("Input: F5 pressed; recompiling deployed AngelScript modules.");
            g_scriptHost.Reload();
        }
        g_reloadKeyIsDown = reloadIsDown;

        if (g_scriptHost.IsLoaded())
        {
            g_scriptHost.Tick(
                static_cast<float>(kHotkeyPollIntervalMilliseconds) / 1000.0f);
        }

        if (g_runtimeConfiguration.Mode() != ClientMode::TransformProbe &&
            g_runtimeConfiguration.Mode() != ClientMode::AppearanceCycle)
        {
            return;
        }

        const bool isDown = (GetAsyncKeyState('1') & 0x8000) != 0;
        DWORD foregroundProcessId = 0;
        const HWND foregroundWindow = GetForegroundWindow();
        if (foregroundWindow != nullptr)
        {
            GetWindowThreadProcessId(foregroundWindow, &foregroundProcessId);
        }

        if (foregroundProcessId == GetCurrentProcessId())
        {
            const bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            ObserveOneKeyState(isDown, shiftPressed, "timer key-state polling");
        }
        else
        {
            g_oneKeyIsDown = isDown;
        }

        ExecutePendingTransform();
    }

    void OnGameWindowTimer()
    {
        PollHotkey();
    }

    void OnGameWindowFocusChanged(bool focused)
    {
        Log(focused
            ? "Event: Fable game window received keyboard focus."
            : "Event: Fable game window lost keyboard focus.");
    }

    void OnGameWindowDestroyed()
    {
        Log("Event: Fable game window is being destroyed.");
        LogEvent("ShutdownStarted", "game-window-destroyed");
    }

    bool OnGameWindowCloseRequested()
    {
        if (g_runtimeConfiguration.Scenario().empty())
        {
            return false;
        }
        Log("Automation: close requested; posting WM_QUIT to the game-window thread.");
        LogEvent("ShutdownStarted", "automation-window-close");
        PostQuitMessage(0);
        return true;
    }

    void OnGameWindowNumberRowOne(bool down, bool shiftPressed)
    {
        ObserveOneKeyState(
            down,
            shiftPressed,
            down ? "WM_KEYDOWN" : "WM_KEYUP");
    }

    DWORD WINAPI BootstrapThread(void*)
    {
        g_runtimeConfiguration.LoadFromEnvironment();
        g_diagnosticLog.Initialize(
            g_clientModule,
            g_runtimeConfiguration.EventPath().c_str(),
            g_runtimeConfiguration.RunId().c_str(),
            g_runtimeConfiguration.Scenario().c_str());
        g_diagnosticLog.AttachConsole();
        Log("FableTogether client loaded.");
        LogStartupContext();
        const bool appearanceScriptEnabled =
            g_runtimeConfiguration.Mode() == ClientMode::AppearanceCycle ||
            ScenarioIs(L"appearance_cycle");
        LogEvent(
            "ClientReady",
            g_runtimeConfiguration.Mode() == ClientMode::TransformProbe
                ? "transform_probe"
                : appearanceScriptEnabled ? "appearance_cycle" : "observe");

        if (!InitializeCharacterSnapshot())
        {
            Log("Client disabled because the server-character snapshot is invalid.");
            return 9;
        }

        if (g_runtimeConfiguration.Mode() == ClientMode::TransformProbe ||
            appearanceScriptEnabled)
        {
            const PVOID exceptionObserver = AddVectoredExceptionHandler(1, ObserveProcessException);
            LogFormat(
                "Hook: low-address access-violation observer %s; handler=%p max_events=8.",
                exceptionObserver != nullptr ? "installed" : "failed",
                exceptionObserver);
        }

        if (!ValidateExecutable())
        {
            Log("Probe disabled because target validation failed.");
            return 1;
        }

        const fable::core::Diagnostics scriptDiagnostics = {
            ScriptLog,
            ScriptEvent,
        };
        if (!g_scriptHost.Initialize(g_clientModule, g_gameModule, scriptDiagnostics))
        {
            Log("AngelScript gameplay framework initialization failed.");
            LogEvent("ClientFailed", "script-runtime-initialization");
            return 13;
        }

        if (g_runtimeConfiguration.Mode() != ClientMode::TransformProbe &&
            !g_creatureConstructorHook.Install(g_gameModule, scriptDiagnostics))
        {
            Log("Creature lifecycle observation disabled because the constructor hook failed.");
            LogEvent("ClientFailed", "creature-constructor-hook-installation");
            return 12;
        }

        if (appearanceScriptEnabled &&
            !g_aiBrainUpdateObserver.Install(g_gameModule, scriptDiagnostics))
        {
            Log("Creature AI observation disabled because the CAIBrain update hook failed.");
            LogEvent("ClientFailed", "ai-brain-update-observer-installation");
            return 15;
        }

        if (appearanceScriptEnabled &&
            !g_physicsNavigatorObserver.Install(g_gameModule, scriptDiagnostics))
        {
            Log("Creature locomotion observation disabled because the navigator hook failed.");
            LogEvent("ClientFailed", "physics-navigator-observer-installation");
            return 16;
        }

        if (appearanceScriptEnabled &&
            !g_followCreatureActionHook.Install(g_gameModule, scriptDiagnostics))
        {
            Log("Creature locomotion observation disabled because the follow-action hook failed.");
            LogEvent("ClientFailed", "follow-action-hook-installation");
            return 14;
        }

        if (appearanceScriptEnabled &&
            !g_creatureModeManagerObserver.Install(
                g_gameModule,
                scriptDiagnostics))
        {
            Log("Creature locomotion observation disabled because the mode-manager hook failed.");
            LogEvent("ClientFailed", "creature-mode-manager-observer-installation");
            return 17;
        }

        if (appearanceScriptEnabled)
        {
            g_appearanceCycleScenario.Initialize(
                g_scriptHost,
                scriptDiagnostics);
        }

        if (g_runtimeConfiguration.Mode() == ClientMode::Observe &&
            !g_documentsFolderRedirectHook.Install(
                g_gameModule,
                g_runtimeConfiguration.FixtureDocumentsPath().c_str(),
                scriptDiagnostics))
        {
            Log("Automation disabled because the isolated fixture Documents redirect failed.");
            LogEvent("ClientFailed", "fixture-documents-redirect-installation");
            return 8;
        }

        const fable::ui::front_end::FrontEndLifecycleCallbacks
            frontEndLifecycleCallbacks = {
                ObserveUiPageDoBegin,
                ObserveUiPageDoInit,
                ObserveUiPageStartPlay,
                ObservePlayLoadMapMovie,
                ObserveFrontEndStartDoInit,
                ObserveFrontEndStartDoTick,
            };
        if (g_runtimeConfiguration.Mode() == ClientMode::Observe &&
            !g_frontEndLifecycleHooks.Install(
                g_gameModule,
                scriptDiagnostics,
                frontEndLifecycleCallbacks))
        {
            Log("Lifecycle observation disabled because one or more hook signatures failed.");
            LogEvent("ClientFailed", "lifecycle-hook-installation");
            return 5;
        }

        if (g_runtimeConfiguration.Mode() == ClientMode::Observe &&
            !g_frontEndStartInitializerHook.Install(
                g_gameModule,
                scriptDiagnostics,
                OnFrontEndStartInitialized))
        {
            Log("Lifecycle observation disabled because the front-end initializer hook failed.");
            LogEvent("ClientFailed", "front-end-native-init-hook-installation");
            return 6;
        }

        if (g_runtimeConfiguration.Mode() == ClientMode::TransformProbe &&
            !g_heroTransformCompatibilityHooks.Install(
                g_gameModule,
                scriptDiagnostics))
        {
            Log("Probe disabled because the isolated Hero transform compatibility hooks failed.");
            return 3;
        }

        const fable::core::Diagnostics mainWindowDiagnostics = {
            Log,
            LogEvent,
        };
        g_gameWindow = g_mainWindowHook.WaitForWindow(mainWindowDiagnostics);
        g_gameWindowThreadId = GetWindowThreadProcessId(g_gameWindow, nullptr);
        LogEvent("GameWindowReady", "selected");

        if (g_runtimeConfiguration.Mode() == ClientMode::Observe &&
            !g_gameThreadIdleHook.Install(
                g_gameModule,
                g_gameWindowThreadId,
                scriptDiagnostics,
                OnGameThreadIdle))
        {
            Log("Lifecycle observation disabled because the game-thread queue hook failed.");
            LogEvent("ClientFailed", "game-thread-queue-hook-installation");
            return 7;
        }

        const fable::ui::MainWindowCallbacks mainWindowCallbacks = {
            OnGameWindowTimer,
            OnGameWindowFocusChanged,
            OnGameWindowDestroyed,
            OnGameWindowCloseRequested,
            OnGameWindowNumberRowOne,
        };
        const bool captureNumberRowOne =
            g_runtimeConfiguration.Mode() == ClientMode::TransformProbe ||
            g_runtimeConfiguration.Mode() == ClientMode::AppearanceCycle;
        if (!g_mainWindowHook.Install(
            g_gameWindow,
            kHotkeyTimerId,
            kHotkeyPollIntervalMilliseconds,
            captureNumberRowOne,
            mainWindowCallbacks,
            mainWindowDiagnostics))
        {
            Log("Hook: MainWindow installation failed; client disabled.");
            return 2;
        }

        if (g_runtimeConfiguration.Mode() == ClientMode::TransformProbe)
        {
            Log("Transformation probe ready. Press 1 to cycle; press Shift+1 to restore CREATURE_HERO.");
            Log("Hero acquisition uses direct SCRIPT_NAME_HERO lookup; no active quest-script context is required.");
        }
        else if (appearanceScriptEnabled)
        {
            Log("AngelScript creature-puppet cycle ready. Press 1 to cycle forms; press Shift+1 to restore the Hero.");
            Log("The authoritative Hero remains alive while the script controls a native creature through the public Entity and Creature APIs.");
        }
        else
        {
            Log("Lifecycle observation mode ready; transformation hooks and hotkeys are disabled.");
        }
        LogEvent(
            "ClientHooksReady",
            g_runtimeConfiguration.Mode() == ClientMode::TransformProbe
                ? "window-and-transform-probe"
                : appearanceScriptEnabled
                    ? "window-observation-and-angelscript-puppet"
                    : "window-observation-only");
        return 0;
    }
}

extern "C" __declspec(dllexport) const wchar_t* __cdecl FableTogetherVersion()
{
    return kClientVersion;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        static_assert(sizeof(void*) == 4, "FableTogether.Client must be built for Win32.");
        static_assert(sizeof(ScriptThing) == 12, "Unexpected Fable script handle layout.");

        g_clientModule = module;
        DisableThreadLibraryCalls(module);
        const HANDLE thread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
        if (thread != nullptr)
        {
            CloseHandle(thread);
        }
    }

    return TRUE;
}
