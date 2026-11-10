#pragma once

#include "Multiplayer/Protocol/AuthorityMessage.h"
#include "Multiplayer/Protocol/EntityActionMessage.h"

#include <cstdint>
#include <string>

namespace fable::multiplayer::replication::entity_actions
{
    [[nodiscard]] protocol::EntityActionKind Classify(
        const std::string& nativeActionType) noexcept;
    [[nodiscard]] std::uint8_t DefaultFlags(
        protocol::EntityActionKind kind) noexcept;
    [[nodiscard]] bool IsMapScoped(
        protocol::EntityActionKind kind) noexcept;
    [[nodiscard]] unsigned int Priority(
        const protocol::EntityActionMessage& message) noexcept;
    [[nodiscard]] unsigned int LeasePriority(
        protocol::ActionLeaseKind kind) noexcept;
}
