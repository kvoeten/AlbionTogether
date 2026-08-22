#include "CreatureActionFunctions.h"

#include "Game/Creature/Actions/Hooks/CreatureActionLifecycleObserver.h"

#include <cstring>

namespace fable::game::creature::actions::native
{
    bool CreatureActionFunctions::ResolveUpdate(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        return Resolve(
            gameModule,
            UpdateAddressRva,
            UpdateExceptionHandlerRva,
            address);
    }

    bool CreatureActionFunctions::ResolveSubmit(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        return Resolve(
            gameModule,
            SubmitAddressRva,
            SubmitExceptionHandlerRva,
            address);
    }

    bool CreatureActionFunctions::ResolveFinish(
        HMODULE gameModule,
        std::uint8_t*& address) noexcept
    {
        return Resolve(
            gameModule,
            FinishAddressRva,
            FinishExceptionHandlerRva,
            address);
    }

    bool CreatureActionFunctions::SubmitImmediateAttack(
        HMODULE gameModule,
        void* attacker,
        void* target) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule;
        (void)attacker;
        (void)target;
        return false;
#else
        if (gameModule == nullptr || attacker == nullptr || target == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        std::uint8_t* constructorAddress = nullptr;
        std::uint8_t* destructorAddress = nullptr;
        auto* const submitAddress = reinterpret_cast<std::uint8_t*>(
            base + SubmitAddressRva);
        if (!Resolve(
                gameModule,
                ImmediateAttackConstructorAddressRva,
                ImmediateAttackConstructorExceptionHandlerRva,
                constructorAddress) ||
            !Resolve(
                gameModule,
                ActionDestructorAddressRva,
                ActionDestructorExceptionHandlerRva,
                destructorAddress))
        {
            return false;
        }

        // The lifecycle observer is installed before multiplayer starts and
        // replaces this entry with a near jump. Calling that entry is correct:
        // the observer records the action and forwards through its verified
        // trampoline. Accept either that installed detour or pristine retail
        // bytes for callers that initialize without the observer.
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

        void** const attackVtable = reinterpret_cast<void**>(
            base + ImmediateAttackActionVtableRva);
        bool vtableValid = false;
        __try
        {
            vtableValid = attackVtable[0] == reinterpret_cast<void*>(
                base + ImmediateAttackActionFirstMethodRva);
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
            ImmediateAttackConstructorPointer>(constructorAddress);
        const auto submit = reinterpret_cast<SubmitPointer>(submitAddress);
        const auto destroy = reinterpret_cast<ActionDestructorPointer>(
            destructorAddress);
        alignas(void*) unsigned char action[ImmediateAttackStorageSize] = {};
        std::uintptr_t actionContext[2] = {};
        bool constructed = false;
        bool submitted = false;
        __try
        {
            construct(
                action,
                attacker,
                target,
                ImmediateAttackActionTime,
                actionContext);
            *reinterpret_cast<void***>(action) = attackVtable;
            constructed = true;
            submitted = submit(attacker, action);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitted = false;
        }

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
        return submitted && destroyed;
#endif
    }

    bool CreatureActionFunctions::SubmitUntargetedAttack(
        HMODULE gameModule,
        void* attacker,
        const float (&targetPosition)[3]) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule;
        (void)attacker;
        (void)targetPosition;
        return false;
#else
        if (gameModule == nullptr || attacker == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        std::uint8_t* constructorAddress = nullptr;
        auto* const submitAddress = reinterpret_cast<std::uint8_t*>(
            base + SubmitAddressRva);
        if (!Resolve(
                gameModule,
                UntargetedAttackConstructorAddressRva,
                UntargetedAttackConstructorExceptionHandlerRva,
                constructorAddress))
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

        void** const attackVtable = reinterpret_cast<void**>(
            base + UntargetedAttackActionVtableRva);
        bool vtableValid = false;
        __try
        {
            vtableValid = attackVtable[0] == reinterpret_cast<void*>(
                base + UntargetedAttackActionDeletingDestructorRva);
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
            UntargetedAttackConstructorPointer>(constructorAddress);
        const auto submit = reinterpret_cast<SubmitPointer>(submitAddress);
        const auto destroy = reinterpret_cast<
            ActionDeletingDestructorPointer>(attackVtable[0]);
        alignas(void*) unsigned char action[UntargetedAttackStorageSize] = {};
        void* actionContext = nullptr;
        bool constructed = false;
        bool submitted = false;
        __try
        {
            construct(
                action,
                attacker,
                nullptr,
                targetPosition,
                &actionContext);
            constructed = true;
            submitted = submit(attacker, action);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitted = false;
        }

        bool destroyed = !constructed;
        if (constructed)
        {
            __try
            {
                // Scalar deleting destructors use bit zero to request the
                // allocation be freed. This action lives in stack storage.
                destroy(action, 0u);
                destroyed = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                destroyed = false;
            }
        }
        return submitted && destroyed;
#endif
    }

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
        CreatureActionLifecycleObserver::BeginAuthoritativeReplay();
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
        CreatureActionLifecycleObserver::EndAuthoritativeReplay();
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


    bool CreatureActionFunctions::Resolve(
        HMODULE gameModule,
        std::uintptr_t addressRva,
        std::uintptr_t exceptionHandlerRva,
        std::uint8_t*& address) noexcept
    {
        address = nullptr;
        if (gameModule == nullptr)
        {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const candidate = reinterpret_cast<std::uint8_t*>(base + addressRva);
        std::uintptr_t exceptionHandler = 0;
        __try
        {
            if (std::memcmp(
                    candidate,
                    ExpectedPrefix.data(),
                    ExpectedPrefix.size()) != 0)
            {
                return false;
            }
            std::memcpy(
                &exceptionHandler,
                candidate + ExpectedPrefix.size(),
                sizeof(exceptionHandler));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        if (exceptionHandler != base + exceptionHandlerRva)
        {
            return false;
        }
        address = candidate;
        return true;
    }
}
