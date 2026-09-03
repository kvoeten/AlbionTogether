#pragma once

#include "Automation/AppearanceCycle/AppearanceCycleScenario.h"
#include "Automation/CharacterSnapshot/ServerCharacterSnapshot.h"
#include "Automation/FixtureDocuments/Hooks/DocumentsFolderRedirectHook.h"
#include "Automation/LocalInstance/Hooks/ForegroundWindowHook.h"
#include "Automation/LocalInstance/Hooks/UnrealSingletonHook.h"
#include "Automation/Runtime/RuntimeConfiguration.h"
#include "Core/Diagnostics/DiagnosticLog.h"
#include "Core/GameThread/Hooks/GameThreadIdleHook.h"
#include "Game/Creature/AI/Hooks/AiBrainUpdateObserver.h"
#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Creature/Hooks/CreatureConstructorHook.h"
#include "Game/Creature/Locomotion/Hooks/CreatureModeManagerObserver.h"
#include "Game/Creature/Locomotion/Hooks/FollowCreatureActionHook.h"
#include "Game/Creature/Locomotion/Hooks/PhysicsNavigatorObserver.h"
#include "Game/Entity/Presence/Hooks/ThingPresenceObserver.h"
#include "Game/Entity/Persistence/Hooks/SavedEntityMapBlobObserver.h"
#include "Game/Entity/Persistence/Hooks/ThingSaveProjectionHook.h"
#include "Game/HeroPawn/TransformProbe/Hooks/HeroTransformCompatibilityHooks.h"
#include "Game/NPC/Population/Hooks/PopulationSimulationHook.h"
#include "Game/World/Travel/Hooks/WorldTravelObserver.h"
#include "Core/Bootstrap/FeatureRegistry.h"
#include "Game/Runtime/GameplayRuntime.h"
#include "UI/FrontEnd/Hooks/FrontEndLifecycleHooks.h"
#include "UI/FrontEnd/Hooks/FrontEndStartInitializerHook.h"
#include "UI/MainWindow/MainWindowHook.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace fable::core::bootstrap
{
    struct CountedPointerInfo final
    {
        unsigned int referenceCount;
        void(__fastcall* deleteFunction)(void*);
        void* data;
    };

    struct ScriptThing final
    {
        void** vtable;
        void* implementation;
        CountedPointerInfo* pointerInfo;
    };

    struct CharString final { void* stringData; };
    struct GameScriptInterface final { void** vtable; };

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
    using ClientMode = fable::automation::runtime::ClientMode;

    struct CharacterState final
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

    struct CreatureComponentEntry final
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

    enum class PendingTransform { None, Cycle, Restore };

    struct NativeFault final
    {
        DWORD code = ERROR_SUCCESS;
        void* instructionAddress = nullptr;
        ULONG_PTR operation = 0;
        void* accessedAddress = nullptr;
        const char* stage = "unknown";
    };

    enum class ClientRuntimeStatus : DWORD
    {
        NotInitialized = 0,
        PreResumeReady = 1,
        Starting = 2,
        Ready = 3,
        Failed = 4,
        Stopping = 5,
        Stopped = 6,
    };

    enum class FeatureInstallStage : std::uint8_t
    {
        PreResume,
        PostResume,
    };

    struct ClientRuntimeContext;

    struct FeatureLifecycleContext final
    {
        ClientRuntimeContext* runtime = nullptr;
        FeatureInstallStage stage = FeatureInstallStage::PreResume;
    };

    struct CoreRuntimeContext final
    {
        HMODULE clientModule = nullptr;
        HMODULE gameModule = nullptr;
        fable::core::DiagnosticLog diagnosticLog;
        fable::automation::runtime::RuntimeConfiguration configuration;
        HANDLE bootstrapThread = nullptr;
        DWORD bootstrapThreadId = 0;
        HANDLE cancelEvent = nullptr;
        HANDLE resumeEvent = nullptr;
        HANDLE completionEvent = nullptr;
        std::atomic<ClientRuntimeStatus> status{ClientRuntimeStatus::NotInitialized};
        std::atomic<DWORD> failureCode{ERROR_SUCCESS};
    };

    struct DiagnosticsRuntimeContext final
    {
        PVOID vectoredExceptionHandler = nullptr;
        std::atomic_uint lowAddressAccessViolationsLogged{0};
    };

    struct NativeHooksRuntimeContext final
    {
        fable::automation::fixture_documents::DocumentsFolderRedirectHook documents;
        fable::automation::local_instance::UnrealSingletonHook singleton;
        fable::core::game_thread::GameThreadIdleHook gameThreadIdle;
        fable::game::creature::ai::AiBrainUpdateObserver aiBrain;
        fable::game::creature::actions::CreatureActionLifecycleObserver creatureActions;
        fable::game::entity::presence::ThingPresenceObserver thingPresence;
        fable::game::entity::persistence::SavedEntityMapBlobObserver savedEntityMap;
        fable::game::entity::persistence::ThingSaveProjectionHook thingSave;
        fable::game::npc::population::PopulationSimulationHook population;
        fable::game::world::travel::WorldTravelObserver worldTravel;
        fable::game::creature::CreatureConstructorHook creatureConstructor;
        fable::game::creature::locomotion::CreatureModeManagerObserver creatureModes;
        fable::game::creature::locomotion::FollowCreatureActionHook followCreature;
        fable::game::creature::locomotion::PhysicsNavigatorObserver physicsNavigator;
        fable::game::hero_pawn::transform_probe::HeroTransformCompatibilityHooks transformCompatibility;
        fable::ui::front_end::FrontEndLifecycleHooks frontEndLifecycle;
        fable::ui::front_end::FrontEndStartInitializerHook frontEndInitializer;
    };

    struct GameplayRuntimeContext final
    {
        fable::game::GameplayRuntime runtime;
    };

    struct CharacterSnapshotRuntimeContext final
    {
        ServerCharacterSnapshot snapshot;
        bool configured = false;
        std::atomic_uint stableSamples{0};
        std::atomic_bool assertionPassed{false};
        std::atomic_bool applied{false};
        std::atomic_bool snapshotAssertionPassed{false};
        std::atomic_uint verificationAttempts{0};
        CharacterState baseline;
        unsigned int baselineStableSamples = 0;
        std::atomic<ULONGLONG> heroLastProbeAt{0};
        std::atomic_uint heroProbeCount{0};
        std::atomic_bool heroReadyLogged{false};
    };

    struct FrontEndAutomationRuntimeContext final
    {
        std::atomic_uint uiLifecycleEventsLogged{0};
        std::atomic_bool readyLogged{false};
        std::atomic<ULONGLONG> readyAt{0};
        std::atomic<void*> mainMenuObject{nullptr};
        std::atomic<ULONGLONG> mainMenuLastTickAt{0};
        std::atomic_uint mainMenuTickCount{0};
        std::atomic_uint mainMenuLastLoggedPhase{0xFFFFFFFFu};
        std::atomic_bool mainMenuReady{false};
        std::atomic_bool startInvoked{false};
        std::atomic<ULONGLONG> startInvokedAt{0};
        std::atomic<void*> loadGamePageObject{nullptr};
        std::atomic_bool saveListRequested{false};
        std::atomic_bool saveListReadyLogged{false};
        std::atomic_bool saveListBeginAttempted{false};
        std::atomic<ULONGLONG> saveListLastTickAt{0};
        std::atomic_uint saveListTickCount{0};
        std::atomic_uint saveListLastLoggedPhase{0xFFFFFFFFu};
        std::atomic_bool fixtureSaveSelected{false};
        std::atomic_uint fixtureSaveIdentity{0xFFFFFFFFu};
        std::atomic<ULONGLONG> fixtureSaveSelectedAt{0};
        std::atomic_bool fixtureStartInvoked{false};
        std::atomic<ULONGLONG> fixtureStartInvokedAt{0};
        std::atomic_bool mainMenuReleased{false};
        std::array<std::atomic<void*>, 4> startObjects = {};
        std::atomic_uint passStartStage{0};
        std::atomic_uint passStartAttempts{0};
        std::atomic<ULONGLONG> startReadyAt{0};
        std::atomic<ULONGLONG> passStartKeyDownAt{0};
        std::atomic<ULONGLONG> passStartSubmittedAt{0};
        std::atomic_bool shutdownStarted{false};
    };

    struct TransformProbeRuntimeContext final
    {
        std::atomic_size_t creatureIndex{0};
        std::array<ScriptThing, 64> retainedHandles = {};
        std::size_t retainedHandleCount = 0;
        bool oneKeyIsDown = false;
        bool reloadKeyIsDown = false;
        bool firstTimerTickLogged = false;
        std::atomic<PendingTransform> pending{PendingTransform::None};
        unsigned int interfaceProbeTicks = 0;
        int lastInterfaceReadyState = -1;
        int lastSteamApiReadyState = -1;
    };

    struct AutomationRuntimeContext final
    {
        fable::automation::appearance_cycle::AppearanceCycleScenario appearanceCycle;
        CharacterSnapshotRuntimeContext characterSnapshot;
        FrontEndAutomationRuntimeContext frontEnd;
        TransformProbeRuntimeContext transformProbe;
    };

    struct UiRuntimeContext final
    {
        HWND gameWindow = nullptr;
        DWORD gameWindowThreadId = 0;
        fable::ui::MainWindowHook mainWindow;
        fable::automation::local_instance::ForegroundWindowHook foregroundWindow;
    };

    struct ClientRuntimeContext final
    {
        CoreRuntimeContext core;
        DiagnosticsRuntimeContext diagnostics;
        NativeHooksRuntimeContext nativeHooks;
        GameplayRuntimeContext gameplay;
        AutomationRuntimeContext automation;
        UiRuntimeContext ui;
        FeatureLifecycleContext preResumeLifecycle;
        FeatureLifecycleContext postResumeLifecycle;
        FeatureContext preResumeFeatureContext;
        FeatureContext postResumeFeatureContext;
        FeatureInstallTransaction preResumeFeatures;
        FeatureInstallTransaction postResumeFeatures;
    };
}
