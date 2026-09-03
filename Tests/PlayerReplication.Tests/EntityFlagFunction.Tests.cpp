#include "Game/Entity/Native/EntityFlagFunction.h"

#include <algorithm>
#include <array>

int RunEntityFlagFunctionTests()
{
    using namespace fable::game::entity::native;
    int failures = 0;
    // Pin the current Anniversary ABI, not the historical FSE interface.
    failures += FindEntityFlagFunction(469) != nullptr;
    const auto* const attackable = FindEntityFlagFunction(477);
    const auto* const persistent = FindEntityFlagFunction(496);
    failures += attackable == nullptr || attackable->addressRva != 0x0188EA20;
    failures += persistent == nullptr || persistent->addressRva != 0x0188AE90;

    for (const auto& function : EntityFlagFunctions)
    {
        std::array<std::uint8_t, 128> code = {};
        std::copy(function.prefix.begin(), function.prefix.end(), code.begin());
        code[function.returnOffset] = 0xC2;
        code[function.returnOffset + 1] = 8;
        const auto moduleBase =
            reinterpret_cast<std::uintptr_t>(code.data()) - function.addressRva;
        failures += !function.Matches(moduleBase, code.data());
        failures += function.Matches(0, code.data());
        failures += function.Matches(moduleBase, code.data() + 1);

        // The crash-producing native call popped six arguments (24 bytes).
        code[function.returnOffset + 1] = 24;
        failures += function.Matches(moduleBase, code.data());
        code[function.returnOffset + 1] = 8;
        code[0] ^= 0xFF;
        failures += function.Matches(moduleBase, code.data());
    }
    return failures;
}
