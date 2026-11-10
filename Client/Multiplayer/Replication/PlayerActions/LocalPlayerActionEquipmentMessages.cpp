#include "Multiplayer/Replication/PlayerActions/LocalPlayerActionCapture.h"

#include "Multiplayer/Replication/LocalHeroReplication.h"
#include "Multiplayer/Transport/UdpPeer.h"

#include <cstdio>

namespace fable::multiplayer::replication
{
    bool LocalPlayerActionCapture::CaptureLocalWeaponTransition(
        const game::creature::actions::CreatureActionLifecycleEvent& action,
        const game::hero_pawn::equipment::HeroEquipmentState& equipment)
    {
        const PlayerState* const state = localHero_->CurrentState();
        if (!action.accepted || action.animationId == 0 ||
            action.creature == nullptr ||
            action.creature != localHero_->NativeHero() ||
            !equipment.IsSane() || state == nullptr ||
            state->actorId != localActorId_ || state->authorityEpoch == 0 ||
            state->actorGeneration == 0 || state->mapEpoch == 0 ||
            state->mapName.empty())
        {
            return true;
        }

        const std::uint64_t transitionActionId = NextActionId();
        const protocol::SessionTimeMs startedAt = protocol::ToSessionTime(
            transport_->LocalToSessionTimeMilliseconds(action.observedAt));
        char detail[512] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "actor_id=%llu action_id=%llu family=%u melee=%d melee_slot=%u ranged=%d ranged_slot=%u native_action=%s animation_id=%u map=%s phase=%s",
            static_cast<unsigned long long>(localActorId_),
            static_cast<unsigned long long>(transitionActionId),
            static_cast<unsigned int>(equipment.activeFamily),
            equipment.meleeDefinitionIndex,
            equipment.meleeAttachmentSlot,
            equipment.rangedDefinitionIndex,
            equipment.rangedAttachmentSlot,
            action.actionType,
            action.animationId,
            state->mapName.c_str(),
            role_ == PeerRole::Host ? "perform" : "intent");
        diagnostics_.Event("MultiplayerLocalWeaponTransitionCaptured", detail);
        localHero_->MarkEquipmentTransition(
            transitionActionId,
            startedAt,
            action.animationId,
            WeaponTransitionDurationMilliseconds,
            WeaponAttachmentNotifyMilliseconds);
        return true;
    }

}
