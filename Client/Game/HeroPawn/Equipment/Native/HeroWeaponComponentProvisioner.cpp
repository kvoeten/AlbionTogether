#include "HeroWeaponComponentProvisioner.h"

#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/Entity/Native/ThingComponentProvisioner.h"
#include "Game/HeroPawn/Equipment/Native/HeroWeaponComponent.h"

namespace
{
    constexpr char kHeroCoreComponentName[] = "CTCHero";
    constexpr char kHeroWeaponInventoryComponentName[] =
        "CTCInventoryWeapons";
}

namespace fable::game::hero_pawn::equipment::native
{
    bool HeroWeaponComponentProvisioner::Ensure(
        HMODULE gameModule,
        void* hero,
        HeroWeaponComponentProvisioning& result) noexcept
    {
        result = {};
        if (gameModule == nullptr || hero == nullptr ||
            !entity::native::ThingComponentProvisioner::IsSupported(
                gameModule))
        {
            return false;
        }

        result.heroCore = entity::native::ThingComponentAccess::Find(
            hero, entity::native::ThingComponentType::HeroCore);
        if (result.heroCore == nullptr)
        {
            result.heroCore =
                entity::native::ThingComponentProvisioner::AddNamed(
                    gameModule, hero, kHeroCoreComponentName);
            result.heroCoreAdded = result.heroCore != nullptr;
        }
        void* const typedHeroCore =
            entity::native::ThingComponentAccess::Find(
                hero, entity::native::ThingComponentType::HeroCore);
        if (result.heroCore == nullptr || typedHeroCore == nullptr ||
            result.heroCore != typedHeroCore)
        {
            result = {};
            return false;
        }

        result.component = entity::native::ThingComponentAccess::Find(
            hero, entity::native::ThingComponentType::HeroWeaponInventory);
        if (result.component == nullptr)
        {
            result.component =
                entity::native::ThingComponentProvisioner::AddNamed(
                    gameModule, hero, kHeroWeaponInventoryComponentName);
            result.added = result.component != nullptr;
        }

        void* const typed = entity::native::ThingComponentAccess::Find(
            hero, entity::native::ThingComponentType::HeroWeaponInventory);
        void* const identified =
            entity::native::ThingComponentAccess::FindByVtableRva(
                hero, gameModule, HeroWeaponComponent::ExpectedVtableRva);
        if (result.component == nullptr || typed == nullptr ||
            identified == nullptr || result.component != typed ||
            typed != identified)
        {
            result = {};
            return false;
        }

        result.component = identified;
        result.heroCore = typedHeroCore;
        return true;
    }
}
