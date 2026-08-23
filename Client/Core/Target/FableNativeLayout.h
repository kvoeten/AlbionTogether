#pragma once

#include <cstddef>
#include <cstdint>
#include <Windows.h>

namespace fable::core::target
{
    inline constexpr DWORD kExpectedImageSize = 0x035D5000;
    inline constexpr std::uintptr_t kGameScriptInterfaceSlotRva = 0x031BBC34;
    inline constexpr std::uintptr_t kGameScriptInterfaceVtableRva = 0x02AE35C4;
    inline constexpr std::uintptr_t kCharStringConstructorRva = 0x012B7800;
    inline constexpr std::uintptr_t kCharStringDestructorRva = 0x012B75D0;
    inline constexpr std::uintptr_t kGetHeroRva = 0x01889940;
    inline constexpr std::uintptr_t kGetThingWithScriptNameRva = 0x0189DF10;
    inline constexpr std::uintptr_t kTurnCreatureIntoRva = 0x01898200;
    inline constexpr std::uintptr_t kScriptThingVtableRva = 0x02A5CBF4;
    inline constexpr std::uintptr_t kScriptThingDestructorRva = 0x0135C7A7;
    inline constexpr std::uintptr_t kScriptThingIsNullRva = 0x0135B9E0;
    inline constexpr std::uintptr_t kScriptThingGetPositionVectorRva = 0x0135B93F;
    inline constexpr std::uintptr_t kScriptThingGetFacingAngleRva = 0x0135B994;
    inline constexpr std::uintptr_t kActiveHeroSelectorRva = 0x018FD7D0;
    inline constexpr std::uintptr_t kActiveHeroCreatureResolverRva = 0x018E3AD0;
    inline constexpr std::uintptr_t kHeroProgressionHealthGetMaximumRva = 0x019CC6DA;
    inline constexpr std::uintptr_t kGameScriptInterfaceTeleportThingRva = 0x0189EE20;
    inline constexpr std::uintptr_t kRegionManagerResolveIndexRva = 0x01BC6560;
    inline constexpr std::uintptr_t kThingPlayerCreatureVtableRva = 0x02B1DBB4;
    inline constexpr std::uintptr_t kThingCreatureVtableRva = 0x02B1AFE4;
    inline constexpr std::uintptr_t kThingPlayerCreatureModifyCombatHealthRva = 0x01B5A520;
    inline constexpr std::uintptr_t kFrontEndMainMenuVtableRva = 0x02B302A4;
    inline constexpr std::uintptr_t kFrontEndMainMenuDoBeginRva = 0x01BEFC00;
    inline constexpr std::uintptr_t kFrontEndMainMenuDoTickRva = 0x01BF05A0;
    inline constexpr std::uintptr_t kFrontEndMainMenuDoOnUIEventRva = 0x01BF05F0;
    inline constexpr std::uintptr_t kLoadGamePageVtableRva = 0x02B30D84;
    inline constexpr std::uintptr_t kLoadGamePageDoBeginRva = 0x01BF4F60;
    inline constexpr std::uintptr_t kLoadGamePageDoTickRva = 0x01BF6510;
    inline constexpr std::uintptr_t kLoadGamePageDoStartPlayRva = 0x01BF56C0;
    inline constexpr std::uintptr_t kLoadGamePageDoOnUIEventRva = 0x01BF5C20;
    inline constexpr std::uintptr_t kLocalSaveManagerSlotRva = 0x0322FC00;

    inline constexpr std::size_t kGetHeroVtableIndex = 70;
    inline constexpr std::size_t kGetThingWithScriptNameVtableIndex = 78;
    inline constexpr std::size_t kTurnCreatureIntoVtableIndex = 100;
    inline constexpr std::size_t kThingCreatureModifyCombatHealthVtableIndex = 0x100 / sizeof(void*);
    inline constexpr std::size_t kTeleportThingVtableIndex = 0x7B0 / sizeof(void*);
}
