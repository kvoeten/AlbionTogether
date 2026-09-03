#pragma once

#include <cstddef>
#include <cstdint>

namespace fable::game::native
{
    // Fable Anniversary Win32 executable:
    // SHA-256 2a95eea3c2cce9b47ca0f454a605b6952216f5d25158efd12ba48b70130989f2
    namespace rva
    {
        inline constexpr std::uintptr_t GameScriptInterfaceSlot = 0x031BBC34;
        inline constexpr std::uintptr_t GameScriptInterfaceVtable = 0x02AE35C4;
        inline constexpr std::uintptr_t CharStringConstructor = 0x012B7800;
        inline constexpr std::uintptr_t CharStringDestructor = 0x012B75D0;
        inline constexpr std::uintptr_t WideStringDestructor = 0x0134EFD0;
        inline constexpr std::uintptr_t DefinitionNameByIndex = 0x012CB160;
        inline constexpr std::uintptr_t DefinitionIndexByName = 0x012CC3C0;
        inline constexpr std::uintptr_t DefinitionByIndex = 0x01719230;
        inline constexpr std::uintptr_t DefinitionNameToCharString =
            0x012CABB0;
        inline constexpr std::uintptr_t ParentDefinitionGetInstantiationName =
            0x01379510;
        inline constexpr std::uintptr_t WeakThingPointerGet = 0x012E6EA0;
        inline constexpr std::uintptr_t PerformExpressionActionVtable =
            0x02AD1724;
        inline constexpr std::uintptr_t PerformExtendedExpressionActionVtable =
            0x02AD1844;
        inline constexpr std::uintptr_t ThingManagerSlot = 0x0322F1B0;
        inline constexpr std::uintptr_t GetHero = 0x01889940;
        inline constexpr std::uintptr_t GetThingWithScriptName = 0x0189DF10;
        inline constexpr std::uintptr_t GetThingWithUid = 0x01890660;
        inline constexpr std::uintptr_t CreateCreature = 0x018AF800;
        inline constexpr std::uintptr_t StartScriptingEntity = 0x0189EC00;
        inline constexpr std::uintptr_t ScriptedControlImplementationVtable = 0x02AE9F4C;
        inline constexpr std::uintptr_t ScriptedControlDeleteFunction = 0x01B0B0F8;
        inline constexpr std::uintptr_t ThingRequestDestroy = 0x01B2E530;
        inline constexpr std::uintptr_t ThingAddComponent = 0x01B2F3F0;
        inline constexpr std::uintptr_t ThingCreatureApplyDefinition =
            0x01B30B50;
        inline constexpr std::uintptr_t ThingCreatureDefinitionConstructor =
            0x01865310;
        inline constexpr std::uintptr_t ThingCreatureDefinitionVtable =
            0x02ADFD4C;
        inline constexpr std::uintptr_t GameHeapFree = 0x00001060;
        inline constexpr std::uintptr_t TurnCreatureInto = 0x01898200;
        inline constexpr std::uintptr_t TeleportThing = 0x0189EE20;
        inline constexpr std::uintptr_t OpenDoor = 0x0189A230;
        inline constexpr std::uintptr_t SetAttackImmediately = 0x018AF5F0;
        inline constexpr std::uintptr_t SheatheWeapons = 0x0188E6A0;
        inline constexpr std::uintptr_t UnsheatheWeapons = 0x0188E720;
        inline constexpr std::uintptr_t UnsheatheMeleeWeapon = 0x0188E790;
        inline constexpr std::uintptr_t UnsheatheRangedWeapon = 0x0188E890;
        inline constexpr std::uintptr_t AddScreenMessage = 0x018916A0;
        inline constexpr std::uintptr_t DisplayQuestInfo = 0x0188FFC0;
        inline constexpr std::uintptr_t AddQuestInfoBar = 0x018901C0;
        inline constexpr std::uintptr_t UpdateQuestInfoBar = 0x018903A0;
        inline constexpr std::uintptr_t RemoveQuestInfoElement = 0x01890460;
        inline constexpr std::uintptr_t SetNoDialogCamera = 0x0188ADF0;
        inline constexpr std::uintptr_t ActivateQuest = 0x01891D90;
        inline constexpr std::uintptr_t IsQuestActive = 0x01891E50;
        inline constexpr std::uintptr_t IsQuestRegistered = 0x01891E60;
        inline constexpr std::uintptr_t IsQuestCompleted = 0x01891E70;
        inline constexpr std::uintptr_t IsQuestFailed = 0x01891E80;
        // Global quest-manager/save seam from the current PE32 build. The
        // manager instance and CStringParser lifecycle remain deliberately
        // opaque until a runtime-safe construction boundary is validated.
        inline constexpr std::uintptr_t QuestManagerGlobal = 0x03230360;
        inline constexpr std::uintptr_t QuestManagerSaveGameState =
            0x01BC4270;
        inline constexpr std::uintptr_t QuestManagerLoadGameState =
            0x01BC5200;
        inline constexpr std::uintptr_t PersistLoadGameState = 0x01BC5EF0;
        // Loads the retail save bundle in section order: ENTITIES, PLAYER,
        // QUESTS, REGIONS, then FACTIONS. Multiplayer uses its completion
        // boundary so host-owned sections cannot be overwritten later by
        // the guest save's own section loaders.
        inline constexpr std::uintptr_t GameStateBundleLoadSections =
            0x01BA2BA0;
        inline constexpr std::uintptr_t CStringParserConstructor =
            0x012C0AA0;
        inline constexpr std::uintptr_t CStringParserDestructor =
            0x012C0B90;
        inline constexpr std::uintptr_t AutoSave = 0x0188C510;
        inline constexpr std::uintptr_t WorldSaveGameStateManual =
            0x01BA35B0;
        inline constexpr std::uintptr_t UserProfileManager = 0x01B9B3C0;
        inline constexpr std::uintptr_t UserProfileGetAutoSavePathName =
            0x01B918F0;
        inline constexpr std::uintptr_t ScriptThingVtable = 0x02A5CBF4;
        inline constexpr std::uintptr_t ScriptThingDestructor = 0x0135C7A7;
        inline constexpr std::uintptr_t ScriptThingGetName = 0x0135B8C4;
        inline constexpr std::uintptr_t ScriptThingGetDefinitionName = 0x0135B8D6;
        inline constexpr std::uintptr_t ScriptThingGetDataString = 0x0135B903;
        inline constexpr std::uintptr_t ScriptThingSetDataString = 0x0135B930;
        inline constexpr std::uintptr_t ScriptThingGetPosition = 0x0135B93F;
        inline constexpr std::uintptr_t ScriptThingGetFacing = 0x0135B994;
        inline constexpr std::uintptr_t ScriptThingGetCurrentMapName = 0x0135C30F;
        inline constexpr std::uintptr_t ScriptThingGetHomeMapName = 0x0135C33C;
        inline constexpr std::uintptr_t ScriptThingGetUid = 0x0135C369;
        inline constexpr std::uintptr_t ScriptThingIsNull = 0x0135B9E0;
        inline constexpr std::uintptr_t ScriptThingIsSneaking = 0x0135C398;
        inline constexpr std::uintptr_t ScriptThingIsAwareOfHero = 0x0135C3A7;
        inline constexpr std::uintptr_t ScriptThingIsUnconscious = 0x0135C669;
        inline constexpr std::uintptr_t ScriptThingIsUsable = 0x0135C67B;
        inline constexpr std::uintptr_t ScriptThingIsOpenDoor = 0x0135C6A1;
        inline constexpr std::uintptr_t ScriptThingIsSummonedCreature = 0x0135C6B3;
        inline constexpr std::uintptr_t ScriptThingSetAsUsable = 0x0135C6C5;
        inline constexpr std::uintptr_t ScriptThingSetFriendsWithEverything = 0x0135C6D7;
        inline constexpr std::uintptr_t ScriptThingGetActivationTriggerStatus = 0x0135C6E9;
        inline constexpr std::uintptr_t ScriptThingSetActivationTriggerStatus = 0x0135C6FB;
        inline constexpr std::uintptr_t ScriptThingSetToKillOnLevelUnload = 0x0135C70D;
        inline constexpr std::uintptr_t ScriptThingUpdateAttachment = 0x0135C71F;
        inline constexpr std::uintptr_t ScriptThingIncrementScriptCounter = 0x0135C72F;
        inline constexpr std::uintptr_t ScriptThingDecrementScriptCounter = 0x0135C73F;
        inline constexpr std::uintptr_t ScriptThingGetScriptCounter = 0x0135C74F;
        inline constexpr std::uintptr_t ThingCreatureVtable = 0x02B1AFE4;
        inline constexpr std::uintptr_t ThingPlayerCreatureVtable = 0x02B1DBB4;
    }

    namespace game_interface_slot
    {
        inline constexpr std::size_t StartScriptingEntity = 12;
        inline constexpr std::size_t GetHero = 70;
        inline constexpr std::size_t GetThingWithScriptName = 78;
        inline constexpr std::size_t GetThingWithUid = 89;
        inline constexpr std::size_t CreateCreature = 97;
        inline constexpr std::size_t TurnCreatureInto = 100;
        // Current Anniversary layout, verified from the native cutscene
        // .SetAttackable call at RVA 0x0136097A. Older FSE slot numbers differ;
        // slot 469 here takes six arguments, not (Thing, bool).
        inline constexpr std::size_t SetAttackable = 0x774 / sizeof(void*);
        inline constexpr std::size_t SetPersistent = 0x7C0 / sizeof(void*);
        inline constexpr std::size_t SetBound = 0x804 / sizeof(void*);
        inline constexpr std::size_t SetFree = 0x808 / sizeof(void*);
        inline constexpr std::size_t SetDrawable = 0x848 / sizeof(void*);
        inline constexpr std::size_t SetDamageable = 0x85C / sizeof(void*);
        inline constexpr std::size_t SetCollidable = 0x898 / sizeof(void*);
        inline constexpr std::size_t TeleportThing = 0x7B0 / sizeof(void*);
        inline constexpr std::size_t OpenDoor = 444;
        inline constexpr std::size_t SetAttackImmediately = 479;
        inline constexpr std::size_t SheatheWeapons = 525;
        inline constexpr std::size_t UnsheatheWeapons = 526;
        inline constexpr std::size_t UnsheatheMeleeWeapon = 527;
        inline constexpr std::size_t UnsheatheRangedWeapon = 528;
        inline constexpr std::size_t AddScreenMessage = 118;
        inline constexpr std::size_t DisplayQuestInfo = 337;
        inline constexpr std::size_t AddQuestInfoBar = 340;
        inline constexpr std::size_t UpdateQuestInfoBar = 348;
        inline constexpr std::size_t RemoveQuestInfoElement = 354;
        inline constexpr std::size_t SetNoDialogCamera = 0x614 / sizeof(void*);
        inline constexpr std::size_t ActivateQuest = 291;
        inline constexpr std::size_t IsQuestActive = 299;
        inline constexpr std::size_t IsQuestRegistered = 300;
        inline constexpr std::size_t IsQuestCompleted = 301;
        inline constexpr std::size_t IsQuestFailed = 302;
        inline constexpr std::size_t AutoSave = 728;
    }

    namespace script_thing_slot
    {
        inline constexpr std::size_t Destructor = 0;
        inline constexpr std::size_t GetName = 1;
        inline constexpr std::size_t GetDefinitionName = 2;
        inline constexpr std::size_t GetDataString = 3;
        inline constexpr std::size_t SetDataString = 4;
        inline constexpr std::size_t GetPosition = 6;
        inline constexpr std::size_t GetCurrentMapName = 8;
        inline constexpr std::size_t GetHomeMapName = 9;
        inline constexpr std::size_t GetFacing = 10;
        inline constexpr std::size_t ResolveNative = 11;
        inline constexpr std::size_t GetUid = 12;
        inline constexpr std::size_t IsSneaking = 15;
        inline constexpr std::size_t IsAwareOfHero = 16;
        inline constexpr std::size_t IsUnconscious = 61;
        inline constexpr std::size_t IsUsable = 62;
        inline constexpr std::size_t IsOpenDoor = 64;
        inline constexpr std::size_t IsSummonedCreature = 65;
        inline constexpr std::size_t SetAsUsable = 66;
        inline constexpr std::size_t SetFriendsWithEverything = 67;
        inline constexpr std::size_t GetActivationTriggerStatus = 68;
        inline constexpr std::size_t SetActivationTriggerStatus = 69;
        inline constexpr std::size_t SetToKillOnLevelUnload = 70;
        inline constexpr std::size_t UpdateAttachment = 71;
        inline constexpr std::size_t IncrementScriptCounter = 72;
        inline constexpr std::size_t DecrementScriptCounter = 73;
        inline constexpr std::size_t GetScriptCounter = 74;
        inline constexpr std::size_t IsAlive = 75;
        inline constexpr std::size_t IsDead = 76;
        inline constexpr std::size_t IsNull = 77;
    }

    namespace thing_creature_slot
    {
        inline constexpr std::size_t ModifyCombatHealth = 0x100 / sizeof(void*);
    }

    namespace scripted_control_slot
    {
        inline constexpr std::size_t FireProjectileAt = 3;
        inline constexpr std::size_t MoveToPosition = 4;
        inline constexpr std::size_t MoveToEntity = 5;
        inline constexpr std::size_t FollowRoute = 6;
        inline constexpr std::size_t FollowEntity = 7;
        inline constexpr std::size_t StopFollowing = 8;
        inline constexpr std::size_t IsFollowActionRunning = 9;
        inline constexpr std::size_t ClearCommands = 10;
        inline constexpr std::size_t PerformExpression = 17;
        inline constexpr std::size_t PlayAnimation = 18;
        inline constexpr std::size_t PlayCombatAnimation = 19;
        inline constexpr std::size_t PlayLoopingAnimation = 20;
        inline constexpr std::size_t ClearAllActions = 21;
        inline constexpr std::size_t ClearAllActionsIncludingLoops = 22;
        inline constexpr std::size_t UnsheatheWeapons = 25;
        inline constexpr std::size_t IsPerformingScriptTask = 26;
        inline constexpr std::size_t IsFollowingEntity = 27;
        inline constexpr std::size_t Wait = 29;
        inline constexpr std::size_t IsNull = 30;
    }
}
