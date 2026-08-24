#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::creature::combat::native
{
    struct CreatureDeathFunctions final
    {
        struct SubmissionResult final
        {
            bool invoked = false;
            bool accepted = false;
            bool cleanupSucceeded = false;
        };

        using ConstructorPointer = void* (__thiscall*)(
            void* action,
            void* creature,
            bool alignToGround);
        using SubmitPointer = bool(__thiscall*)(void* creature, void* action);
        using ScalarDeletingDestructorPointer = void* (__thiscall*)(
            void* action,
            unsigned int flags);

        static constexpr std::uintptr_t ConstructorRva = 0x017C03F0;
        static constexpr std::uintptr_t ConstructorExceptionHandlerRva =
            0x0250F2C8;
        static constexpr std::uintptr_t VtableRva = 0x02ACAC34;
        static constexpr std::uintptr_t DeletingDestructorRva = 0x017E0CD0;
        static constexpr std::size_t StorageSize = 0xB8;
        static constexpr std::uintptr_t SubmitActionRva = 0x01B42F70;
        static constexpr std::array<std::uint8_t, 3> ConstructorPrefix = {
            0x6A, 0xFF, 0x68};

        static SubmissionResult Submit(
            HMODULE gameModule,
            void* creature) noexcept;

    private:
        static bool Validate(HMODULE gameModule) noexcept;
    };
}
