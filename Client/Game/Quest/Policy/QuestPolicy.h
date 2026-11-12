#pragma once

#include <cstdint>

namespace fable::game::quest::policy
{
    enum class Lifecycle : std::uint8_t
    {
        Shared,
        LocalOnly,
        HostOnly,
        Blocked,
    };

    enum class World : std::uint8_t
    {
        None,
        MapOwner,
    };

    enum class Decision : std::uint8_t
    {
        None,
        InitiatorShared,
    };

    enum class Transition : std::uint8_t
    {
        None,
        Shared,
    };

    enum class Reward : std::uint8_t
    {
        None,
        HeroLocal,
        SharedWorld,
    };

    enum class Confidence : std::uint8_t
    {
        Verified,
        Inferred,
        NeedsTrace,
    };

    struct QuestPolicy final
    {
        const char* name = nullptr;
        Lifecycle lifecycle = Lifecycle::Blocked;
        World world = World::None;
        Decision decision = Decision::None;
        Transition transition = Transition::None;
        Reward reward = Reward::None;
        Confidence confidence = Confidence::NeedsTrace;
    };
}
