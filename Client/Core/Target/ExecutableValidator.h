#pragma once

#include <Windows.h>

namespace fable::core::target
{
    using ValidationLog = void (*)(const char* message);

    [[nodiscard]] bool ValidateFableExecutable(
        HMODULE gameModule,
        ValidationLog log) noexcept;
}

