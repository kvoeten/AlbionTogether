#include "GameDeveloperToolAdapter.h"

#include "../../Game/Entity/Entity.h"
#include "../../Game/Entity/EntityService.h"
#include "../../Game/NPC/NpcService.h"
#include "../../Game/Quest/QuestService.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace fable::developer_tools
{
    GameDeveloperToolAdapter::GameDeveloperToolAdapter(
        game::EntityService* entities,
        game::NpcService* npcs,
        game::QuestService* quests,
        IDeveloperWorldAuthority* worldAuthority) noexcept
        : entities_(entities), npcs_(npcs), quests_(quests),
          worldAuthority_(worldAuthority)
    {
    }

    void GameDeveloperToolAdapter::Bind(
        game::EntityService* entities,
        game::NpcService* npcs,
        game::QuestService* quests,
        IDeveloperWorldAuthority* worldAuthority) noexcept
    {
        entities_ = entities;
        npcs_ = npcs;
        quests_ = quests;
        worldAuthority_ = worldAuthority;
    }

    void GameDeveloperToolAdapter::SetHostAuthorized(bool authorized) noexcept
    {
        hostAuthorized_.store(authorized, std::memory_order_release);
    }

    bool GameDeveloperToolAdapter::IsHostAuthorized() const noexcept
    {
        return hostAuthorized_.load(std::memory_order_acquire);
    }

    void GameDeveloperToolAdapter::SetSessionIdentity(
        const std::uint64_t sessionIdentity) noexcept
    {
        sessionIdentity_.store(sessionIdentity, std::memory_order_release);
        nextSpawnSequence_.store(1, std::memory_order_release);
    }

    DeveloperToolResult GameDeveloperToolAdapter::Rejected(
        DeveloperCommandKind command) const noexcept
    {
        DeveloperToolResult result;
        result.command = command;
        result.code = IsHostAuthorized()
            ? DeveloperToolResultCode::Unavailable
            : DeveloperToolResultCode::Rejected;
        result.detail = DeveloperToolText::From(
            IsHostAuthorized() ? "service unavailable" : "host authorization required");
        return result;
    }

    DeveloperToolResult GameDeveloperToolAdapter::SpawnEntity(
        const SpawnEntityCommand& command) noexcept
    {
        if (!IsHostAuthorized()) return Rejected(DeveloperCommandKind::SpawnEntity);
        if (npcs_ == nullptr) return Rejected(DeveloperCommandKind::SpawnEntity);
        if (command.definition.Empty() ||
            !std::isfinite(command.position.x) ||
            !std::isfinite(command.position.y) ||
            !std::isfinite(command.position.z))
        {
            return Rejected(DeveloperCommandKind::SpawnEntity);
        }
        const std::uint64_t sessionIdentity = sessionIdentity_.load(
            std::memory_order_acquire);
        if (sessionIdentity == 0)
        {
            return Rejected(DeveloperCommandKind::SpawnEntity);
        }

        const game::Vector3 position{command.position.x, command.position.y, command.position.z};
        std::uint64_t sequence = nextSpawnSequence_.fetch_add(
            1, std::memory_order_relaxed);
        if (sequence == 0)
        {
            sequence = nextSpawnSequence_.fetch_add(
                1, std::memory_order_relaxed);
        }
        char scriptName[64] = {};
        std::snprintf(scriptName, sizeof(scriptName),
            "ALBION_DEV_%016llX_%llu",
            static_cast<unsigned long long>(sessionIdentity),
            static_cast<unsigned long long>(sequence));
        game::Entity* entity = npcs_->Spawn(
            std::string(command.definition.value.data()), position,
            scriptName);
        if (entity == nullptr) return Rejected(DeveloperCommandKind::SpawnEntity);

        const std::uint64_t entityUid = entity->GetUid();
        if (entityUid == 0)
        {
            entity->Release();
            return Rejected(DeveloperCommandKind::SpawnEntity);
        }

        DeveloperToolResult result;
        result.command = DeveloperCommandKind::SpawnEntity;
        result.code = DeveloperToolResultCode::Accepted;
        result.entityUid = entityUid;
        result.detail = DeveloperToolText::From("entity spawned");
        entity->Release();
        return result;
    }

    DeveloperToolResult GameDeveloperToolAdapter::TeleportEntity(
        const TeleportEntityCommand& command) noexcept
    {
        if (!IsHostAuthorized()) return Rejected(DeveloperCommandKind::TeleportEntity);
        if (entities_ == nullptr) return Rejected(DeveloperCommandKind::TeleportEntity);
        if (!std::isfinite(command.position.x) ||
            !std::isfinite(command.position.y) ||
            !std::isfinite(command.position.z))
        {
            return Rejected(DeveloperCommandKind::TeleportEntity);
        }

        game::Entity* entity = entities_->FindByUid(command.entityUid);
        if (entity == nullptr) return Rejected(DeveloperCommandKind::TeleportEntity);
        const game::Vector3 position{command.position.x, command.position.y, command.position.z};
        const bool moved = entity->Teleport(position, entity->GetFacing());
        entity->Release();

        DeveloperToolResult result;
        result.command = DeveloperCommandKind::TeleportEntity;
        result.code = moved ? DeveloperToolResultCode::Accepted : DeveloperToolResultCode::Rejected;
        result.entityUid = command.entityUid;
        result.detail = DeveloperToolText::From(moved ? "entity teleported" : "teleport rejected");
        return result;
    }

    DeveloperToolResult GameDeveloperToolAdapter::UseRegionExit(
        const UseRegionExitCommand& command) noexcept
    {
        if (!IsHostAuthorized()) return Rejected(DeveloperCommandKind::UseRegionExit);
        if (entities_ == nullptr) return Rejected(DeveloperCommandKind::UseRegionExit);
        game::Entity* entity = entities_->FindByUid(command.exitUid);
        if (entity == nullptr) return Rejected(DeveloperCommandKind::UseRegionExit);
        const char* failure = nullptr;
        const bool used = entity->UseScriptedAction(true, &failure);
        entity->Release();
        DeveloperToolResult result;
        result.command = DeveloperCommandKind::UseRegionExit;
        result.code = used ? DeveloperToolResultCode::Accepted : DeveloperToolResultCode::Rejected;
        result.entityUid = command.exitUid;
        result.detail = DeveloperToolText::From(used ? "region exit used" : (failure != nullptr ? failure : "region exit rejected"));
        return result;
    }

    DeveloperToolResult GameDeveloperToolAdapter::QueryQuest(
        const QuestCommand& command) noexcept
    {
        if (!IsHostAuthorized()) return Rejected(DeveloperCommandKind::QueryQuest);
        if (quests_ == nullptr) return Rejected(DeveloperCommandKind::QueryQuest);

        const std::string name(command.questName.value.data());
        DeveloperToolResult result;
        result.command = DeveloperCommandKind::QueryQuest;
        result.code = DeveloperToolResultCode::Accepted;
        result.questKnown = quests_->IsRegistered(name);
        if (!result.questKnown)
        {
            result.detail = DeveloperToolText::From("quest unknown");
            return result;
        }
        char status[DeveloperToolTextCapacity] = {};
        std::snprintf(status, sizeof(status),
            "quest registered active=%s completed=%s failed=%s",
            quests_->IsActive(name) ? "yes" : "no",
            quests_->IsCompleted(name) ? "yes" : "no",
            quests_->IsFailed(name) ? "yes" : "no");
        result.detail = DeveloperToolText::From(status);
        return result;
    }

    DeveloperToolResult GameDeveloperToolAdapter::ActivateQuest(
        const ActivateQuestCommand& command) noexcept
    {
        if (!IsHostAuthorized()) return Rejected(DeveloperCommandKind::ActivateQuest);
        if (quests_ == nullptr) return Rejected(DeveloperCommandKind::ActivateQuest);

        const bool activated = quests_->Activate(
            std::string(command.questName.value.data()));
        const bool published = activated && worldAuthority_ != nullptr &&
            worldAuthority_->PublishQuestState();
        DeveloperToolResult result;
        result.command = DeveloperCommandKind::ActivateQuest;
        result.code = published ? DeveloperToolResultCode::Accepted
            : activated ? DeveloperToolResultCode::Unavailable
                        : DeveloperToolResultCode::Rejected;
        result.detail = DeveloperToolText::From(
            published ? "quest activated and host snapshot published"
                      : activated ? "quest activated; host snapshot capture failed"
                                  : "quest activation rejected");
        return result;
    }

    DeveloperToolResult GameDeveloperToolAdapter::QuerySaveSection(
        const SaveSectionCommand& command) noexcept
    {
        if (!IsHostAuthorized()) return Rejected(DeveloperCommandKind::QuerySaveSection);
        DeveloperToolResult result;
        result.command = DeveloperCommandKind::QuerySaveSection;
        if (worldAuthority_ == nullptr ||
            !worldAuthority_->ReadSaveSectionStatus(
            command.section, result.fingerprint, result.detail))
        {
            result.code = DeveloperToolResultCode::Unavailable;
            result.detail = DeveloperToolText::From("save section status unavailable");
            return result;
        }
        result.code = DeveloperToolResultCode::Accepted;
        return result;
    }
}
