#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::game::quest
{
    enum class QuestLifecycleOperation : std::uint8_t
    {
        Activate,
        Deactivate,
        Complete,
        Fail,
    };

    struct QuestLifecycleState final
    {
        bool active = false;
        bool completed = false;
        bool failed = false;
    };

    // A pointer-free value emitted after retail accepted and applied a real
    // lifecycle transition. It is deliberately independent from transport;
    // Multiplayer may copy it into its own bounded event queue.
    struct QuestLifecycleEvent final
    {
        static constexpr std::size_t MaximumNameBytes = 128;

        QuestLifecycleOperation operation = QuestLifecycleOperation::Activate;
        std::array<char, MaximumNameBytes> name = {};
        std::array<std::uint32_t, 3> arguments = {};
        QuestLifecycleState before = {};
        QuestLifecycleState after = {};
        std::uint32_t threadId = 0;

        [[nodiscard]] const char* Name() const noexcept
        {
            return name.data();
        }
    };
}
