#include "CreatureActionFunctions.h"

#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"
#include "Game/Creature/Actions/CreatureActionSubmissionContext.h"

#include <cstring>

namespace fable::game::creature::actions::native
{
    bool CreatureActionFunctions::SubmitWeaponTransition(
        HMODULE gameModule,
        void* creature,
        game::creature::equipment::CreatureWeaponFamily family) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule;
        (void)creature;
        (void)family;
        return false;
#else
        using game::creature::equipment::CreatureWeaponFamily;
        std::int32_t mode = 0;
        if (family == CreatureWeaponFamily::None)
        {
            mode = 1;
        }
        else if (family == CreatureWeaponFamily::Melee)
        {
            mode = 4;
        }
        else if (family == CreatureWeaponFamily::Ranged)
        {
            mode = 5;
        }
        else
        {
            return false;
        }
        if (gameModule == nullptr || creature == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const constructorAddress = reinterpret_cast<std::uint8_t*>(
            base + WeaponTransitionConstructorAddressRva);
        auto* const submitAddress = reinterpret_cast<std::uint8_t*>(
            base + SubmitAddressRva);
        std::uint8_t* destructorAddress = nullptr;
        bool constructorValid = false;
        __try
        {
            constructorValid = std::memcmp(
                constructorAddress,
                WeaponTransitionConstructorPrefix.data(),
                WeaponTransitionConstructorPrefix.size()) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            constructorValid = false;
        }
        if (!constructorValid ||
            !Resolve(
                gameModule,
                ActionDestructorAddressRva,
                ActionDestructorExceptionHandlerRva,
                destructorAddress))
        {
            return false;
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
            return false;
        }

        void** const transitionVtable = reinterpret_cast<void**>(
            base + WeaponTransitionActionVtableRva);
        bool vtableValid = false;
        __try
        {
            vtableValid = transitionVtable[0] == reinterpret_cast<void*>(
                base + WeaponTransitionActionFirstMethodRva);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            vtableValid = false;
        }
        if (!vtableValid)
        {
            return false;
        }

        const auto construct = reinterpret_cast<
            WeaponTransitionConstructorPointer>(constructorAddress);
        const auto submit = reinterpret_cast<SubmitPointer>(submitAddress);
        const auto destroy = reinterpret_cast<ActionDestructorPointer>(
            destructorAddress);
        alignas(void*) unsigned char action[WeaponTransitionStorageSize] = {};
        bool constructed = false;
        bool submitted = false;
        bool receiptArmed = false;
        ScopedActionSubmissionOrigin::PushForSehBoundary(
            ActionSubmissionOrigin::ReplicatedPresentation);
        __try
        {
            construct(action, creature, mode);
            constructed = true;
            receiptArmed = CreatureActionLifecycleObserver::
                BeginSubmissionReceipt(creature);
            submitted = receiptArmed && submit(creature, action);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitted = false;
        }
        ScopedActionSubmissionOrigin::PopForSehBoundary(
            ActionSubmissionOrigin::ReplicatedPresentation);
        bool accepted = false;
        const bool observed = receiptArmed && CreatureActionLifecycleObserver::
            EndSubmissionReceipt(creature, accepted);

        bool destroyed = !constructed;
        if (constructed)
        {
            __try
            {
                destroy(action);
                destroyed = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                destroyed = false;
            }
        }
        return submitted && observed && accepted && destroyed;
#endif
    }
}
