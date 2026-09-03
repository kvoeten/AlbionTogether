#include "NpcTransferAcceptanceDriver.h"

#include "Game/Entity/Entity.h"
#include "Game/Entity/EntityService.h"
#include "Game/NPC/NpcService.h"
#include "NativeRegionRoute.h"
#include "Multiplayer/Runtime/MultiplayerSession.h"

#include <Windows.h>

#include <cmath>
#include <cstdio>

namespace
{
    constexpr char TargetDefinition[] = "CREATURE_BS_GUARD";
    constexpr char TargetScriptName[] =
        "SCRIPT_NAME_ALBION_TOGETHER_TRANSFER_TARGET";
    constexpr float SpawnOffset = 2.0f;
    constexpr std::uint64_t SpawnDelayMilliseconds = 500;
    constexpr std::uint64_t SourceRosterSettleMilliseconds = 1'000;
}

namespace fable::automation::multiplayer::transition
{
    void NpcTransferAcceptanceDriver::Initialize(
        bool enabled,
        game::EntityService& entities,
        game::NpcService& npcs,
        ::fable::multiplayer::MultiplayerSession& multiplayer,
        const core::Diagnostics& diagnostics) noexcept
    {
        Shutdown();
        entities_ = &entities;
        npcs_ = &npcs;
        multiplayer_ = &multiplayer;
        diagnostics_ = diagnostics;
        enabled_ = enabled;
        if (enabled_)
        {
            diagnostics_.Event(
                "MultiplayerNpcTransferAcceptanceArmed",
                "host will move one tracked guard through the source Mapwho teardown before both Heroes cross the fixture boundary");
        }
    }

    void NpcTransferAcceptanceDriver::Tick(bool remotePresentationReady)
    {
        if (!enabled_ || completed_ || !remotePresentationReady ||
            entities_ == nullptr || npcs_ == nullptr || multiplayer_ == nullptr)
        {
            return;
        }

        const std::uint64_t now = GetTickCount64();
        if (armedAt_ == 0)
        {
            armedAt_ = now;
            return;
        }
        if (target_ == nullptr)
        {
            if (now - armedAt_ < SpawnDelayMilliseconds)
            {
                return;
            }
            game::Entity* const hero = entities_->GetHero();
            if (hero == nullptr || !hero->IsValid())
            {
                if (hero != nullptr)
                {
                    hero->Release();
                }
                return;
            }
            const game::Vector3 heroPosition = hero->GetPosition();
            const float facing = hero->GetFacing();
            const std::string mapName = hero->GetCurrentMapName();
            hero->Release();
            sourceMapId_ = multiplayer_->Contexts().players.localHero.MapId();
            native_route::Descriptor route;
            if (sourceMapId_ == 0 ||
                !native_route::SelectFirst(
                    *entities_,
                    *multiplayer_,
                    sourceMapId_,
                    route) ||
                !std::isfinite(heroPosition.x) ||
                !std::isfinite(heroPosition.y) ||
                !std::isfinite(heroPosition.z) ||
                !std::isfinite(facing))
            {
                return;
            }
            destinationMapId_ = route.exit.destinationMapId;
            destinationPosition_ = route.destinationPosition;
            destinationFacing_ = route.destinationFacing;
            const game::Vector3 spawnPosition = {
                heroPosition.x + SpawnOffset,
                heroPosition.y,
                heroPosition.z,
            };
            target_ = npcs_->Spawn(
                TargetDefinition,
                spawnPosition,
                TargetScriptName);
            if (target_ == nullptr || !target_->IsValid())
            {
                if (target_ != nullptr)
                {
                    target_->Release();
                    target_ = nullptr;
                }
                return;
            }
            target_->SetDamageable(false);
            target_->SetAttackable(false);
            target_->SetKillOnLevelUnload(true);
            scriptRetained_ = target_->IncrementScriptCounter();
            targetUid_ = target_->GetUid();
            spawnedAt_ = now;

            char detail[320] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "thing_uid=%016llX definition=%s script_name=%s source_map=%s destination_map_id=%u script_retained=%s",
                static_cast<unsigned long long>(targetUid_),
                TargetDefinition,
                TargetScriptName,
                mapName.c_str(),
                static_cast<unsigned int>(destinationMapId_),
                scriptRetained_ ? "true" : "false");
            diagnostics_.Event("MultiplayerNpcTransferTargetSpawned", detail);
            return;
        }

        if (now - spawnedAt_ < SourceRosterSettleMilliseconds)
        {
            return;
        }
        completed_ = true;
        if (!BeginSourceTeardown())
        {
            diagnostics_.Event(
                "ClientFailed",
                "multiplayer-npc-transfer-acceptance-source-teardown");
        }
    }

    bool NpcTransferAcceptanceDriver::BeginSourceTeardown()
    {
        if (target_ == nullptr || entities_ == nullptr ||
            multiplayer_ == nullptr || targetUid_ == 0)
        {
            diagnostics_.Event(
                "MultiplayerNpcTransferSourceTeardownFailed",
                "stage=fixture-state");
            return false;
        }
        if (!target_->IsValid())
        {
            diagnostics_.Event(
                "MultiplayerNpcTransferSourceTeardownFailed",
                "stage=script-handle-invalid");
            return false;
        }
        if (!multiplayer_->TransferOwnedEntity(
                targetUid_,
                destinationMapId_,
                destinationPosition_,
                destinationFacing_))
        {
            diagnostics_.Event(
                "MultiplayerNpcTransferSourceTeardownFailed",
                "stage=canonical-transfer-submit");
            return false;
        }
        if (scriptRetained_)
        {
            target_->DecrementScriptCounter();
            scriptRetained_ = false;
        }

        char detail[256] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "thing_uid=%016llX source_map_id=%u destination_map_id=%u teardown=requested",
            static_cast<unsigned long long>(targetUid_),
            static_cast<unsigned int>(sourceMapId_),
            static_cast<unsigned int>(destinationMapId_));
        diagnostics_.Event("MultiplayerNpcTransferSourceTeardownRequested", detail);
        return true;
    }

    void NpcTransferAcceptanceDriver::Shutdown() noexcept
    {
        if (target_ != nullptr)
        {
            if (scriptRetained_ && target_->IsValid())
            {
                target_->DecrementScriptCounter();
            }
            target_->Release();
        }
        entities_ = nullptr;
        npcs_ = nullptr;
        multiplayer_ = nullptr;
        target_ = nullptr;
        diagnostics_ = {};
        armedAt_ = 0;
        spawnedAt_ = 0;
        targetUid_ = 0;
        sourceMapId_ = 0;
        destinationMapId_ = 0;
        destinationPosition_ = {};
        destinationFacing_ = 0.0f;
        scriptRetained_ = false;
        enabled_ = false;
        completed_ = false;
    }
}
