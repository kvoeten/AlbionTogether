#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::ui::front_end::native
{
    struct FrontEndLifecycleFunctions final
    {
        enum class Boundary : std::size_t
        {
            UiPageDoBegin,
            UiPageDoInit,
            UiPageStartPlay,
            PlayLoadMapMovie,
            FrontEndStartDoInit,
            FrontEndStartDoTick,
            Count,
        };

        struct Definition final
        {
            Boundary boundary;
            std::uintptr_t addressRva;
            const char* name;
        };

        static constexpr std::size_t DisplacedBytes = 7;
        static constexpr std::array<std::uint8_t, DisplacedBytes> ExpectedPrefix = {
            0x8B, 0x44, 0x24, 0x04, 0xFF, 0x40, 0x18,
        };
        static constexpr std::array<
            Definition,
            static_cast<std::size_t>(Boundary::Count)> Definitions = {{
            {Boundary::UiPageDoBegin, 0x004B63D0, "UI page DoBegin exec"},
            {Boundary::UiPageDoInit, 0x004B6440, "UI page DoInit exec"},
            {Boundary::UiPageStartPlay, 0x01C23D80, "UI page StartPlay exec"},
            {Boundary::PlayLoadMapMovie, 0x004DB750, "PlayLoadMapMovie exec"},
            {Boundary::FrontEndStartDoInit, 0x01C23BF0, "front-end start DoInit exec"},
            {Boundary::FrontEndStartDoTick, 0x01C23C20, "front-end start DoTick exec"},
        }};

        using Addresses = std::array<
            std::uint8_t*,
            static_cast<std::size_t>(Boundary::Count)>;

        static bool Resolve(
            HMODULE gameModule,
            Addresses& addresses,
            const char*& failedDefinition) noexcept;
    };
}
