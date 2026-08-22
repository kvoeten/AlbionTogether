#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::entity::native
{
    enum class ThingComponentType : std::int32_t
    {
        PhysicsNavigator = 0x02,
        HeroMorph = 0x03,
        CreatureNavigation = 0x07,
        Targeting = 0x08,
        InventoryClothing = 0x12,
        // CTCInventoryWeapons owns the Hero's melee/ranged smart pointers.
        // Multi Arrow resolves this component before reading its ranged slot.
        HeroWeaponInventory = 0x13,
        ScriptedControl = 0x1F,
        VillageMember = 0x23,
        // CTCSummonedCreature marks creatures whose lifetime is owned by a
        // summon action. CGameScriptThing::IsSummonedCreature resolves this
        // component before testing its active-state bytes.
        SummonedCreature = 0x26,
        // CTCHero owns the core Hero mode flags consumed by progression,
        // Hero weapon refresh, and ranged-ability eligibility.
        HeroCore = 0x29,
        CreatureModeManager = 0x31,
        EntityEvents = 0x42,
        Look = 0x43,
        Carrying = 0x46,
        Enemy = 0x49,
        ParticleEmitter = 0x4B,
        HeroOpinionDeedLog = 0x51,
        AnimationComplex = 0x5A,
        HeroAttachableAppearanceModifiers = 0x5E,
        ActivationTrigger = 0x66,
        // CTCInventoryAbilities owns the Hero's per-ability unlock records.
        HeroAbilityInventory = 0x6F,
        // CTCHeroMorph resolves component 0x78 and uses its CSkeletalMorphDef
        // store to replace HERO-tagged definition/weight entries before
        // submitting the compiled skeletal resource to the actor graphic.
        SkeletalMorphDefinition = 0x78,
        RegionFollower = 0x7A,
        // Hero Will's standard live-action family. Lightning's release path
        // locates this component and requests teardown of its active action.
        HeroWillStandardAction = 0x84,
        HeroWillDoubleStrikeAction = 0x87,
        // Generic held-action family used by Battle Charge/Assassin Rush.
        // Script-dispatched Fireball and Multi Arrow use their own spell
        // components and do not attach this component.
        HeroWillChargedAction = 0x88,
        HeroWillSummonAction = 0x91,
        HeroWillTimeAction = 0x92,
        HeroWillPhysicalShieldAction = 0x93,
        // CTCForcePushPower is attached to CTCSpecialAbilities' power host
        // while Force Push is active. Retail rejects duplicate construction.
        ForcePushPower = 0x96,
        HeroWillGhostSwordAction = 0x9A,
        // CTCSpecialAbilities is the Hero Will command controller. Rival Hero
        // definitions do not include the Hero Will component pair, so remote
        // proxies provision it without installing unrelated local-Hero systems.
        HeroSpecialAbilities = 0xA7,
        // CTCActionUseScriptedHook is the ordinary target-action component
        // used by scripted doors and region entrances. Its component id is
        // returned by the retail class' virtual type-id method.
        ScriptedUseAction = 0xC2,
        // CTCDummyVillager is the retail bounded low-simulation record for a
        // persistent villager. It carries home/work/creature UIDs, the next
        // recreation day/frame, and respawnable/guard flags in saved maps.
        DummyVillager = 0xD6,
    };

    struct ThingComponentEntry final
    {
        std::int32_t type = 0;
        void* instance = nullptr;
    };

    struct ThingComponentRange final
    {
        const ThingComponentEntry* begin = nullptr;
        const ThingComponentEntry* end = nullptr;
        std::size_t count = 0;
    };

    class ThingComponentAccess final
    {
    public:
        static constexpr std::size_t ComponentRangeOffset = 0x44;

        [[nodiscard]] static bool ReadRange(
            void* nativeThing,
            ThingComponentRange& range) noexcept;

        [[nodiscard]] static void* Find(
            void* nativeThing,
            ThingComponentType type) noexcept;

        [[nodiscard]] static void* FindByVtableRva(
            void* nativeThing,
            HMODULE gameModule,
            std::uintptr_t vtableRva) noexcept;

        [[nodiscard]] static bool Has(
            void* nativeThing,
            ThingComponentType type) noexcept;

        [[nodiscard]] static bool IsActiveSummonedCreature(
            void* nativeThing) noexcept;
    };

    static_assert(
        sizeof(ThingComponentEntry) == 8,
        "Unexpected Fable component-entry layout.");
}
