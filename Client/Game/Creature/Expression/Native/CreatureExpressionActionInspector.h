#pragma once

#include <Windows.h>

#include <cstddef>
#include <cstdint>

namespace fable::game::creature::expression::native
{
    struct CreatureExpressionActionDetails final
    {
        void* target = nullptr;
        std::int32_t durationTicks = 0;
        std::int32_t triggerTicks = 0;
        char definition[128] = {};
    };

    // Reads only the stable semantic inputs retained by Fable's native
    // PerformExpression action. Native pointers never leave the synchronous
    // action-observer callback.
    class CreatureExpressionActionInspector final
    {
    public:
        [[nodiscard]] static bool Inspect(
            HMODULE gameModule,
            void* action,
            const char* actionType,
            CreatureExpressionActionDetails& details) noexcept;
    };
}
