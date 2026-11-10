#include "EntityActionSemantics.h"

namespace fable::multiplayer::replication::entity_actions
{
    protocol::EntityActionKind Classify(
        const std::string& nativeActionType) noexcept
    {
        const auto contains = [&nativeActionType](const char* value)
        {
            return nativeActionType.find(value) != std::string::npos;
        };
        if (contains("ConversationAnimation"))
        {
            return protocol::EntityActionKind::ConversationAnimation;
        }
        if (contains("Talk") || contains("Conversation") ||
            contains("Speech"))
        {
            return protocol::EntityActionKind::Conversation;
        }
        if (contains("Shop") || contains("Trade") || contains("Buy") ||
            contains("Sell"))
        {
            return protocol::EntityActionKind::Trade;
        }
        if (contains("Attack") || contains("Combat") || contains("Block") ||
            contains("Shoot") || contains("Strike"))
        {
            return protocol::EntityActionKind::Combat;
        }
        if (contains("Move") || contains("Follow") || contains("Flee") ||
            contains("Wander") || contains("Patrol") ||
            contains("Navigate"))
        {
            return protocol::EntityActionKind::Movement;
        }
        if (contains("Quest") || contains("Cutscene"))
        {
            return protocol::EntityActionKind::QuestOrCutscene;
        }
        return protocol::EntityActionKind::Native;
    }

    std::uint8_t DefaultFlags(protocol::EntityActionKind kind) noexcept
    {
        switch (kind)
        {
        case protocol::EntityActionKind::Conversation:
        case protocol::EntityActionKind::ConversationAnimation:
            return protocol::entity_action_flag::Exclusive |
                protocol::entity_action_flag::ObserverNoCamera;
        case protocol::EntityActionKind::Combat:
        case protocol::EntityActionKind::QuestOrCutscene:
            return protocol::entity_action_flag::Exclusive;
        default:
            return 0;
        }
    }

    bool IsMapScoped(protocol::EntityActionKind kind) noexcept
    {
        return kind == protocol::EntityActionKind::Native ||
            kind == protocol::EntityActionKind::Movement;
    }

    unsigned int Priority(
        const protocol::EntityActionMessage& message) noexcept
    {
        if (message.kind == protocol::EntityActionKind::Combat &&
            message.semanticName == "PlayerAttackEngagement")
        {
            return 4;
        }
        switch (message.kind)
        {
        case protocol::EntityActionKind::Conversation:
        case protocol::EntityActionKind::ConversationAnimation:
        case protocol::EntityActionKind::Trade:
            return 2;
        case protocol::EntityActionKind::Combat:
            return 3;
        case protocol::EntityActionKind::QuestOrCutscene:
            return 5;
        default:
            return 0;
        }
    }

    unsigned int LeasePriority(protocol::ActionLeaseKind kind) noexcept
    {
        switch (kind)
        {
        case protocol::ActionLeaseKind::Ambient:
            return 1;
        case protocol::ActionLeaseKind::Conversation:
            return 2;
        case protocol::ActionLeaseKind::Combat:
            return 3;
        case protocol::ActionLeaseKind::PrimaryAttacker:
            return 4;
        case protocol::ActionLeaseKind::QuestOrCutscene:
            return 5;
        default:
            return 0;
        }
    }
}
