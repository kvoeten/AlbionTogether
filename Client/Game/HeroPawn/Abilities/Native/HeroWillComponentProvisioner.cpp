#include "HeroWillComponentProvisioner.h"

#include "Game/Entity/Native/ThingComponentAccess.h"
#include "Game/Entity/Native/ThingComponentProvisioner.h"
#include "Game/HeroPawn/Abilities/Native/HeroWillAbilityFunctions.h"

namespace
{
    constexpr char kAbilityInventoryComponentName[] =
        "CTCInventoryAbilities";
    constexpr char kSpecialAbilitiesComponentName[] = "CTCSpecialAbilities";
}

namespace fable::game::hero_pawn::abilities::native
{
    bool HeroWillComponentProvisioner::Ensure(
        HMODULE gameModule,
        void* hero,
        HeroWillComponentProvisioning& result) noexcept
    {
        result = {};
        if (gameModule == nullptr || hero == nullptr)
        {
            return false;
        }

        if (!entity::native::ThingComponentProvisioner::IsSupported(
                gameModule))
        {
            return false;
        }

        void* inventory = entity::native::ThingComponentAccess::Find(
            hero, entity::native::ThingComponentType::HeroAbilityInventory);
        if (inventory == nullptr)
        {
            inventory = entity::native::ThingComponentProvisioner::AddNamed(
                gameModule, hero, kAbilityInventoryComponentName);
            result.abilityInventoryAdded = inventory != nullptr;
        }
        void* const identifiedInventory =
            entity::native::ThingComponentAccess::FindByVtableRva(
                hero,
                gameModule,
                HeroWillAbilityFunctions::AbilityInventoryVtableRva);
        if (inventory == nullptr || identifiedInventory == nullptr ||
            inventory != identifiedInventory ||
            HeroWillAbilityFunctions::ReadOwner(inventory) != hero)
        {
            return false;
        }
        result.abilityInventoryPresent = true;

        result.component = HeroWillAbilityFunctions::FindComponent(
            hero, gameModule);
        if (result.component == nullptr)
        {
            result.component =
                entity::native::ThingComponentProvisioner::AddNamed(
                    gameModule, hero, kSpecialAbilitiesComponentName);
            result.added = result.component != nullptr;
        }
        if (result.component == nullptr)
        {
            return false;
        }

        void* const typed = entity::native::ThingComponentAccess::Find(
            hero, entity::native::ThingComponentType::HeroSpecialAbilities);
        void* const identified = HeroWillAbilityFunctions::FindComponent(
            hero, gameModule);
        if (typed == nullptr || identified == nullptr || typed != identified ||
            HeroWillAbilityFunctions::ReadOwner(identified) != hero ||
            HeroWillAbilityFunctions::ReadCreature(identified) != hero)
        {
            return false;
        }

        result.component = identified;
        return true;
    }
}
