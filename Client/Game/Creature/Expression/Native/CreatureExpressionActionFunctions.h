#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::creature::expression::native
{
    // Resolves a CExpressionDef by semantic name and submits Fable's real
    // CCreatureAction_PerformExpression through the creature action stack.
    struct CreatureExpressionActionFunctions final
    {
        struct SubmissionResult final
        {
            bool definitionResolved = false;
            bool animationOverridden = false;
            bool invoked = false;
            bool accepted = false;
            bool cleanupSucceeded = false;
            std::uint32_t locallySelectedAnimationId = 0;
        };

        static SubmissionResult Submit(
            HMODULE gameModule,
            void* performer,
            void* target,
            const char* expressionDefinition,
            std::uint32_t animationId,
            std::int32_t durationTicks,
            std::int32_t triggerTicks) noexcept;

    private:
        static constexpr std::uintptr_t DefinitionManagerGetRva =
            0x0181D600;
        static constexpr std::uintptr_t DefinitionManagerSlotRva =
            0x03228090;
        static constexpr std::uintptr_t ExpressionDefinitionLookupRva =
            0x017192F0;
        static constexpr std::uintptr_t
            ExpressionDefinitionLookupExceptionHandlerRva = 0x0250E558;
        static constexpr std::uintptr_t ActionConstructorRva = 0x017E18E0;
        static constexpr std::uintptr_t ActionConstructorExceptionHandlerRva =
            0x025119C4;
        static constexpr std::uintptr_t ActionVtableRva = 0x02AD1724;
        static constexpr std::uintptr_t ActionDeletingDestructorRva =
            0x017E3510;
        static constexpr std::uintptr_t SubmitActionRva = 0x01B42F70;
        static constexpr std::size_t ActionStorageSize = 0xD4;
        static constexpr std::size_t ActionDurationTicksOffset = 0x10;
        static constexpr std::size_t ActionTriggerTicksOffset = 0x14;
        static constexpr std::size_t ActionAnimationResourceOffset = 0x74;
        static constexpr std::array<std::uint8_t, 3> SehFunctionPrefix = {
            0x6A, 0xFF, 0x68,
        };

        static bool Validate(HMODULE gameModule) noexcept;
        static bool ReleaseDefinition(void*& definition) noexcept;
    };
}
