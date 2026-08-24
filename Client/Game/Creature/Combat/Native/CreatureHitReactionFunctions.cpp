#include "CreatureHitReactionFunctions.h"

#include "Game/Creature/Actions/Native/CreatureActionFunctions.h"

#include <cstring>

namespace fable::game::creature::combat::native
{
    bool CreatureHitReactionFunctions::ValidateConstructor(
        HMODULE gameModule,
        std::uintptr_t addressRva,
        const std::uint8_t* prefix,
        std::size_t prefixSize,
        std::uintptr_t exceptionHandlerRva) noexcept
    {
        if (gameModule == nullptr || prefix == nullptr || prefixSize == 0)
        {
            return false;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        auto* const candidate = reinterpret_cast<std::uint8_t*>(
            base + addressRva);
        bool valid = false;
        __try
        {
            valid = std::memcmp(candidate, prefix, prefixSize) == 0;
            if (valid && exceptionHandlerRva != 0)
            {
                std::uintptr_t exceptionHandler = 0;
                std::memcpy(
                    &exceptionHandler,
                    candidate + 3,
                    sizeof(exceptionHandler));
                valid = exceptionHandler == base + exceptionHandlerRva;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            valid = false;
        }
        return valid;
    }

    bool CreatureHitReactionFunctions::ValidateSubmitAction(
        HMODULE gameModule) noexcept
    {
        if (gameModule == nullptr)
        {
            return false;
        }
        auto* const submit = reinterpret_cast<std::uint8_t*>(
            reinterpret_cast<std::uintptr_t>(gameModule) + SubmitActionRva);
        bool detoured = false;
        __try
        {
            detoured = submit[0] == 0xE9;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            detoured = false;
        }
        if (detoured)
        {
            return true;
        }
        std::uint8_t* pristine = nullptr;
        return ::fable::game::creature::actions::native::
                CreatureActionFunctions::ResolveSubmit(gameModule, pristine) &&
            pristine == submit;
    }

    CreatureHitReactionFunctions::SubmissionResult
        CreatureHitReactionFunctions::Submit(
        HMODULE gameModule,
        void* target,
        void* source,
        const float (&position)[3],
        const float (&direction)[3],
        bool knockdown) noexcept
    {
#if !defined(_M_IX86)
        (void)gameModule;
        (void)target;
        (void)source;
        (void)position;
        (void)direction;
        (void)knockdown;
        return {};
#else
        if (gameModule == nullptr || target == nullptr || source == nullptr)
        {
            return {};
        }
        const auto base = reinterpret_cast<std::uintptr_t>(gameModule);
        const bool constructorValid = knockdown
            ? ValidateConstructor(
                gameModule,
                KnockdownConstructorRva,
                KnockdownConstructorPrefix.data(),
                KnockdownConstructorPrefix.size(),
                KnockdownCtorExceptionHandlerRva)
            : ValidateConstructor(
                gameModule,
                GenericStrikeResponseConstructorRva,
                GenericStrikeResponseConstructorPrefix.data(),
                GenericStrikeResponseConstructorPrefix.size(),
                GenericStrikeResponseCtorExceptionHandlerRva);
        if (!constructorValid || !ValidateSubmitAction(gameModule))
        {
            return {};
        }

        const std::uintptr_t vtableRva = knockdown
            ? KnockdownVtableRva
            : GenericStrikeResponseVtableRva;
        const std::uintptr_t destructorRva = knockdown
            ? KnockdownDeletingDestructorRva
            : GenericStrikeResponseDeletingDestructorRva;
        void** const vtable = reinterpret_cast<void**>(base + vtableRva);
        bool vtableValid = false;
        __try
        {
            vtableValid = vtable[0] == reinterpret_cast<void*>(
                base + destructorRva);
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
            GenericStrikeResponseConstructorPointer>(base + (knockdown
                ? KnockdownConstructorRva
                : GenericStrikeResponseConstructorRva));
        const auto submit = reinterpret_cast<SubmitPointer>(
            base + SubmitActionRva);
        const auto destroy = reinterpret_cast<
            ScalarDeletingDestructorPointer>(base + destructorRva);
        alignas(void*) unsigned char action[
            GenericStrikeResponseStorageSize] = {};
        bool constructed = false;
        bool submitted = false;
        __try
        {
            construct(action, target, source, position, direction);
            constructed = true;
            submitted = submit(target, action);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            submitted = false;
        }

        SubmissionResult result;
        result.invoked = constructed;
        result.accepted = submitted;
        bool destroyed = !constructed;
        if (constructed)
        {
            __try
            {
                // The action is stack storage; flags=0 prevents the scalar
                // deleting destructor from attempting to free it.
                destroy(action, 0u);
                destroyed = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                destroyed = false;
            }
        }
        result.cleanupSucceeded = destroyed;
        return result;
#endif
    }
}
