#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::creature::combat::native
{
    struct CreatureHitReactionFunctions final
    {
        struct SubmissionResult final
        {
            bool invoked = false;
            bool accepted = false;
            bool cleanupSucceeded = false;
        };

        using GenericStrikeResponseConstructorPointer = void* (__thiscall*)(
            void* action,
            void* target,
            void* source,
            const float* position,
            const float* direction);
        using KnockdownConstructorPointer =
            GenericStrikeResponseConstructorPointer;
        using SubmitPointer = bool(__thiscall*)(void* creature, void* action);
        using ScalarDeletingDestructorPointer = void* (__thiscall*)(
            void* action,
            unsigned int flags);

        static constexpr std::uintptr_t GenericStrikeResponseConstructorRva =
            0x017B0050;
        static constexpr std::uintptr_t GenericStrikeResponseVtableRva =
            0x02AC6EC4;
        static constexpr std::uintptr_t GenericStrikeResponseDeletingDestructorRva =
            0x017F5210;
        static constexpr std::size_t GenericStrikeResponseStorageSize = 0x160;

        static constexpr std::uintptr_t KnockdownConstructorRva = 0x017B0080;
        static constexpr std::uintptr_t KnockdownVtableRva = 0x02AC700C;
        static constexpr std::uintptr_t KnockdownDeletingDestructorRva =
            0x017B02F0;
        static constexpr std::size_t KnockdownStorageSize = 0x160;

        static constexpr std::uintptr_t SubmitActionRva = 0x01B42F70;
        static constexpr std::uintptr_t SubmitActionExceptionHandlerRva =
            0x02553CB0;
        static constexpr std::uintptr_t GenericStrikeResponseCtorExceptionHandlerRva =
            0;
        static constexpr std::uintptr_t KnockdownCtorExceptionHandlerRva =
            0x0250E2FE;
        static constexpr std::array<std::uint8_t, 16>
            GenericStrikeResponseConstructorPrefix = {
                0x8B, 0x44, 0x24, 0x10, 0x8B, 0x54, 0x24, 0x08,
                0x56, 0x50, 0x8B, 0x44, 0x24, 0x0C, 0x8B, 0xF1,
            };
        static constexpr std::array<std::uint8_t, 3>
            KnockdownConstructorPrefix = {0x6A, 0xFF, 0x68};
        static constexpr std::array<std::uint8_t, 3>
            SubmitActionPrefix = {0x6A, 0xFF, 0x68};

        static SubmissionResult Submit(
            HMODULE gameModule,
            void* target,
            void* source,
            const float (&position)[3],
            const float (&direction)[3],
            bool knockdown) noexcept;

    private:
        static bool ValidateConstructor(
            HMODULE gameModule,
            std::uintptr_t addressRva,
            const std::uint8_t* prefix,
            std::size_t prefixSize,
            std::uintptr_t exceptionHandlerRva) noexcept;
        static bool ValidateSubmitAction(HMODULE gameModule) noexcept;
    };
}
