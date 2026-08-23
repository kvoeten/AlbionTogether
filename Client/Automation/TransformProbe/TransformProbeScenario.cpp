#include "TransformProbeScenario.h"
#include "Core/Bootstrap/ClientRuntimeServices.h"
#include "Core/Bootstrap/FeatureRegistry.h"
#include "Core/Diagnostics/ProcessExceptionObserver.h"
#include "Core/Target/FableNativeLayout.h"

#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdio>

namespace fable::core::bootstrap
{
using namespace fable::core::target;

    bool ResolveGameInterface(GameScriptInterface*& gameInterface)
    {
        gameInterface = nullptr;
        if (CoreContext().gameModule == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(CoreContext().gameModule);
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
        if (thing.vtable == nullptr || TransformContext().retainedHandleCount >= TransformContext().retainedHandles.size())
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

        TransformContext().retainedHandles[TransformContext().retainedHandleCount] = thing;
        ++TransformContext().retainedHandleCount;
        thing = {};

        if (referenceCountAvailable)
        {
            LogFormat(
                "Native: retained %s handle for process lifetime; slot=%zu/64 ref_count=%u.",
                label,
                TransformContext().retainedHandleCount,
                referenceCount);
        }
        else
        {
            LogFormat(
                "Native: retained %s handle for process lifetime; slot=%zu/64 ref_count=<unavailable>.",
                label,
                TransformContext().retainedHandleCount);
        }
        return true;
    }

}

namespace
{
    using namespace fable::core::bootstrap;

    bool TransformProbeFeatureEnabled(const FeatureContext&) noexcept { return true; }

    bool InstallTransformProbeFeature(FeatureContext& context) noexcept
    {
        if (IsPreResumeStage(context))
        {
            return true;
        }
        const auto mode = CoreContext().configuration.Mode();
        const bool appearance =
            mode == fable::automation::runtime::ClientMode::AppearanceCycle ||
            ScenarioIs(L"appearance_cycle");
        if (mode == fable::automation::runtime::ClientMode::TransformProbe)
        {
            const fable::core::Diagnostics diagnostics = {ScriptLog, ScriptEvent};
            if (!NativeHooksContext().transformCompatibility.Install(
                    CoreContext().gameModule, diagnostics))
            {
                return false;
            }
            Log("Transformation probe ready. Press 1 to cycle; press Shift+1 to restore CREATURE_HERO.");
            Log("Hero acquisition uses direct SCRIPT_NAME_HERO lookup; no active quest-script context is required.");
        }
        else if (appearance)
        {
            Log("AngelScript creature-puppet cycle ready. Press 1 to cycle forms; press Shift+1 to restore the Hero.");
            Log("The authoritative Hero remains alive while the script controls a native creature through the public Entity and Creature APIs.");
        }
        else
        {
            Log("Lifecycle observation mode ready; transformation hooks and hotkeys are disabled.");
        }
        return true;
    }

    void UninstallTransformProbeFeature(FeatureContext& context) noexcept
    {
        if (!IsPreResumeStage(context))
        {
            fable::automation::transform_probe::ShutdownTransformProbe();
            NativeHooksContext().transformCompatibility.Shutdown();
        }
    }

    FABLE_FEATURE_DEPENDENCIES(transformProbeDependencies, "ui.game-window");
    FABLE_FEATURE_DESCRIPTOR(
        fableTransformProbeFeature,
        "automation.transform-probe",
        "Transform probe automation",
        FeaturePhase::Automation,
        0,
        TransformProbeFeatureEnabled,
        transformProbeDependencies,
        std::size(transformProbeDependencies),
        InstallTransformProbeFeature,
        UninstallTransformProbeFeature,
        "transform-probe-installation");
}

namespace fable::automation::transform_probe
{
using namespace fable::core::bootstrap;
using namespace fable::core::target;
using namespace fable::core::diagnostics;

constexpr std::array<const char*, 14> kCreatureCycle = {
    "CREATURE_HERO", "CREATURE_BS_GUARD", "CREATURE_BS_GUARD_CROSSBOW",
    "CREATURE_PRISON_GUARD", "CREATURE_KN_GUARD", "CREATURE_BS_VILLAGER_MALE",
    "CREATURE_BS_VILLAGER_FEMALE", "CREATURE_TRADER_01", "CREATURE_BANDIT_GRUNT",
    "CREATURE_RIVAL_HERO_WHISPER", "CREATURE_RIVAL_HERO_THUNDER",
    "CREATURE_HOBBE_GRUNT", "CREATURE_BALVERINE_EASY", "CREATURE_HERO_CHILD"};
constexpr UINT kHotkeyPollIntervalMilliseconds = 16;

    void ShutdownTransformProbe() noexcept
    {
        const auto base = reinterpret_cast<std::uintptr_t>(CoreContext().gameModule);
        const auto expected = reinterpret_cast<void**>(base + kScriptThingVtableRva);
        const auto destroy = reinterpret_cast<ScriptThingDestructor>(
            base + kScriptThingDestructorRva);
        for (std::size_t index = TransformContext().retainedHandleCount; index > 0; --index)
        {
            ScriptThing& handle = TransformContext().retainedHandles[index - 1];
            if (handle.vtable == expected)
            {
                __try { destroy(&handle, 0); }
                __except (EXCEPTION_EXECUTE_HANDLER) {}
            }
            handle = {};
        }
        TransformContext().retainedHandleCount = 0;
        TransformContext().pending.store(PendingTransform::None, std::memory_order_release);
    }

    TransformResult TransformHero(const char* creatureDefinition)
    {
        if (TransformContext().retainedHandleCount + 2 > TransformContext().retainedHandles.size())
        {
            Log("Native: transformation disabled because the diagnostic handle-retention limit was reached; restart the game.");
            return TransformResult::RetentionLimit;
        }

        GameScriptInterface* gameInterface = nullptr;
        if (!ResolveGameInterface(gameInterface))
        {
            return TransformResult::GameNotReady;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(CoreContext().gameModule);
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
        const std::size_t nextIndex = (TransformContext().creatureIndex.load() + 1) % kCreatureCycle.size();
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
            TransformContext().creatureIndex.store(nextIndex);
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
            TransformContext().creatureIndex.store(0);
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
        if (!TransformContext().pending.compare_exchange_strong(
                expected,
                requested,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
        {
            Log("Input: transformation request ignored because one is already queued.");
            return;
        }

        Log(CoreContext().configuration.Mode() == ClientMode::AppearanceCycle
            ? "Input: appearance change queued on the next game-window timer dispatch."
            : "Input: transformation queued for direct hero lookup on the next game-window timer dispatch.");
    }

    void ExecutePendingTransform()
    {
        const PendingTransform pending = TransformContext().pending.exchange(
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
            CoreContext().configuration.Mode() == ClientMode::AppearanceCycle
                ? "angelscript_puppet"
                : "transform_probe");
        if (CoreContext().configuration.Mode() == ClientMode::AppearanceCycle)
        {
            GameplayContext().runtime.DispatchKeyPressed(
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
        if (isDown && !TransformContext().oneKeyIsDown)
        {
            QueueOneKeyPress(source, shiftPressed);
        }
        TransformContext().oneKeyIsDown = isDown;
    }

    void ProbeGameInterfaceAvailability()
    {
        if (++TransformContext().interfaceProbeTicks < 60)
        {
            return;
        }
        TransformContext().interfaceProbeTicks = 0;

        const int steamApiReadyState = GetModuleHandleW(L"steam_api.dll") == nullptr ? 0 : 1;
        if (steamApiReadyState != TransformContext().lastSteamApiReadyState)
        {
            LogFormat(
                "Startup: steam_api changed to %s.",
                steamApiReadyState != 0 ? "loaded" : "not-loaded");
            TransformContext().lastSteamApiReadyState = steamApiReadyState;
        }

        GameScriptInterface* gameInterface = nullptr;
        const int readyState = ResolveGameInterface(gameInterface) ? 1 : 0;
        if (readyState != TransformContext().lastInterfaceReadyState)
        {
            LogFormat(
                "Startup: game-script interface changed to %s (interface=%p).",
                readyState != 0 ? "ready" : "not-ready",
                gameInterface);
            TransformContext().lastInterfaceReadyState = readyState;
            LogEvent(
                readyState != 0 ? "GameScriptInterfaceReady" : "GameScriptInterfaceUnavailable",
                readyState != 0 ? "validated" : "not-ready");
        }
    }

    void PollHotkey()
    {
        if (!TransformContext().firstTimerTickLogged)
        {
            Log("Event: first game-window timer tick received; polling fallback is alive.");
            TransformContext().firstTimerTickLogged = true;
        }
        ProbeGameInterfaceAvailability();

        const bool reloadIsDown = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
        if (reloadIsDown && !TransformContext().reloadKeyIsDown)
        {
            Log("Input: F5 pressed; recompiling deployed AngelScript modules.");
            GameplayContext().runtime.Reload();
        }
        TransformContext().reloadKeyIsDown = reloadIsDown;

        if (GameplayContext().runtime.IsLoaded())
        {
            GameplayContext().runtime.Tick(
                static_cast<float>(kHotkeyPollIntervalMilliseconds) / 1000.0f);
            // The window timer keeps firing for local multiplayer instances
            // even when UE3 throttles an unfocused creature update. Drive all
            // replicated actors through their physics components here as a
            // background-safe fallback; focused creature frames still own
            // their normal animation update path.
            if (GetForegroundWindow() != UiContext().gameWindow)
            {
                GameplayContext().runtime.DriveReplicatedMovement();
            }
        }

        if (CoreContext().configuration.Mode() != ClientMode::TransformProbe &&
            CoreContext().configuration.Mode() != ClientMode::AppearanceCycle)
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
            TransformContext().oneKeyIsDown = isDown;
        }

        ExecutePendingTransform();
    }

}
