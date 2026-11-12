#pragma once

#include "Game/Quest/Policy/QuestPolicy.h"

#include <cstddef>

namespace fable::game::quest::policy
{
    // Static campaign policy. Every shipped AddQuest registration has one
    // row; unknown/custom quests fail closed until a mod explicitly supplies
    // its own reviewed policy rather than inheriting an unsafe default.
    class QuestPolicyRegistry final
    {
    public:
        [[nodiscard]] static const QuestPolicy* Find(
            const char* questName) noexcept;
        [[nodiscard]] static const QuestPolicy& Resolve(
            const char* questName) noexcept;
        [[nodiscard]] static std::size_t Count() noexcept;
        [[nodiscard]] static bool IsCompleteAndUnique() noexcept;
    };
}
