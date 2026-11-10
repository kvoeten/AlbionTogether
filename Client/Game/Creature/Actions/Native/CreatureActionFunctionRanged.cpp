#include "CreatureActionFunctions.h"

namespace fable::game::creature::actions::native
{
    CreatureActionFunctions::CarriedWeaponActionSubmissionResult
        CreatureActionFunctions::SubmitRangedAim(
        HMODULE gameModule,
        void* creature,
        void* rangedWeapon) noexcept
    {
        return SubmitCarriedWeaponAction(
            gameModule,
            creature,
            rangedWeapon,
            RangedAimConstructorAddressRva,
            RangedAimConstructorExceptionHandlerRva,
            RangedAimActionVtableRva,
            RangedAimActionDeletingDestructorRva);
    }

    CreatureActionFunctions::CarriedWeaponActionSubmissionResult
        CreatureActionFunctions::SubmitRangedFire(
        HMODULE gameModule,
        void* creature,
        void* rangedWeapon) noexcept
    {
        return SubmitCarriedWeaponAction(
            gameModule,
            creature,
            rangedWeapon,
            RangedFireConstructorAddressRva,
            RangedFireConstructorExceptionHandlerRva,
            RangedFireActionVtableRva,
            RangedFireActionDeletingDestructorRva);
    }

    CreatureActionFunctions::CarriedWeaponActionSubmissionResult
        CreatureActionFunctions::SubmitCarriedWeaponAction(
        HMODULE gameModule,
        void* creature,
        void* rangedWeapon,
        const std::uintptr_t constructorRva,
        const std::uintptr_t constructorExceptionHandlerRva,
        const std::uintptr_t vtableRva,
        const std::uintptr_t deletingDestructorRva) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule;
        (void)creature;
        (void)rangedWeapon;
        (void)constructorRva;
        (void)constructorExceptionHandlerRva;
        (void)vtableRva;
        (void)deletingDestructorRva;
        return {};
#else
        if (gameModule == nullptr || creature == nullptr ||
            rangedWeapon == nullptr)
        {
            return {};
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        std::uint8_t* constructorAddress = nullptr;
        auto* const submitAddress = reinterpret_cast<std::uint8_t*>(
            base + SubmitAddressRva);
        if (!Resolve(
                gameModule,
                constructorRva,
                constructorExceptionHandlerRva,
                constructorAddress))
        {
            return {};
        }

        bool submitReady = false;
        __try
        {
            submitReady = submitAddress[0] == 0xE9;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitReady = false;
        }
        if (!submitReady)
        {
            std::uint8_t* pristineSubmit = nullptr;
            submitReady = ResolveSubmit(gameModule, pristineSubmit) &&
                pristineSubmit == submitAddress;
        }
        if (!submitReady)
        {
            return {};
        }

        void** const actionVtable = reinterpret_cast<void**>(
            base + vtableRva);
        bool vtableValid = false;
        __try
        {
            vtableValid = actionVtable[0] == reinterpret_cast<void*>(
                base + deletingDestructorRva);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            vtableValid = false;
        }
        if (!vtableValid)
        {
            return {};
        }

        const auto construct = reinterpret_cast<
            CarriedWeaponActionConstructorPointer>(constructorAddress);
        const auto submit = reinterpret_cast<SubmitPointer>(submitAddress);
        const auto destroy = reinterpret_cast<
            ActionDeletingDestructorPointer>(actionVtable[0]);
        alignas(void*) unsigned char action[RangedFireStorageSize] = {};
        CarriedWeaponActionSubmissionResult result;
        __try
        {
            construct(action, creature, rangedWeapon);
            result.invoked = true;
            result.accepted = submit(creature, action);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            result.accepted = false;
        }

        if (result.invoked)
        {
            __try
            {
                // SubmitAction clones the ordinary action. Keep the caller's
                // stack storage local by suppressing scalar deallocation.
                destroy(action, 0u);
                result.cleanupSucceeded = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                result.cleanupSucceeded = false;
            }
        }
        return result;
#endif
    }
}
