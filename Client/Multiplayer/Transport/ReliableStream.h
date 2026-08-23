#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace fable::multiplayer
{
    enum class ReliableStreamKind : std::uint8_t
    {
        Control = 0,
        Actor = 1,
        Entity = 2,
        World = 3,
    };

    struct ReliableStreamId final
    {
        ReliableStreamKind kind = ReliableStreamKind::Control;
        std::uint64_t subject = 0;

        constexpr bool operator==(const ReliableStreamId& other) const noexcept
        {
            return kind == other.kind && subject == other.subject;
        }
        constexpr bool operator!=(const ReliableStreamId& other) const noexcept
        {
            return !(*this == other);
        }
    };

    namespace reliable_stream
    {
        inline constexpr ReliableStreamId Control = {
            ReliableStreamKind::Control,
            0};

        // One globally ordered world-lifecycle transaction lane. Baseline
        // boundaries and their entity records must never be split across
        // independently ordered entity streams.
        inline constexpr ReliableStreamId WorldLifecycle = {
            ReliableStreamKind::World,
            0};

        [[nodiscard]] constexpr ReliableStreamId Actor(
            const std::uint64_t actorId) noexcept
        {
            return {ReliableStreamKind::Actor, actorId};
        }

        [[nodiscard]] constexpr ReliableStreamId Entity(
            const std::uint64_t entityUid) noexcept
        {
            return {ReliableStreamKind::Entity, entityUid};
        }
    }
}

template<>
struct std::hash<fable::multiplayer::ReliableStreamId>
{
    std::size_t operator()(
        const fable::multiplayer::ReliableStreamId& stream) const noexcept
    {
        const std::size_t subject =
            std::hash<std::uint64_t>{}(stream.subject);
        return subject ^
            (static_cast<std::size_t>(stream.kind) +
                static_cast<std::size_t>(0x9E3779B9u) +
                (subject << 6) + (subject >> 2));
    }
};
