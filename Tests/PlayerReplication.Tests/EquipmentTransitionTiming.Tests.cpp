#include "Multiplayer/Protocol/EquipmentTransitionTiming.h"
#include "Multiplayer/Replication/PlayerActorLifecycleReducer.h"

#include <cstdint>

int RunEquipmentTransitionTimingTests()
{
    using namespace fable::multiplayer::protocol;
    using fable::multiplayer::replication::PlayerActorLifecycleReducer;
    using namespace equipment_transition_timing;
    int failures = 0;
    const auto require = [&failures](const bool condition)
    {
        if (!condition)
        {
            ++failures;
        }
    };

    require(HasValidMetadata(7, 1'000, 22, 1'000, 200));
    require(!HasValidMetadata(7, 1'000, 22, 1'000, 1'001));
    require(Evaluate(1'100, 1'000, 1'000) == Phase::Active);
    require(Evaluate(999, 1'000, 1'000) == Phase::Future);
    require(Evaluate(2'001, 1'000, 1'000) == Phase::Expired);

    std::uint64_t localStart = 0;
    require(ProjectStartToLocal(5'000, 5'100, 5'000, localStart));
    require(localStart == 4'900);
    require(ProjectStartToLocal(5'000, 5'100, 5'300, localStart));
    require(localStart == 5'200);
    require(!ProjectStartToLocal(5'000, 5'100, 11'000, localStart));
    require(!ProjectStartToLocal(
        5'000, 70'000, 1'000, localStart));

    PlayerActorStateMessage equipmentPatch;
    equipmentPatch.operation = PlayerActorStateOperation::ComponentDelta;
    equipmentPatch.componentFlags =
        player_actor_state_flag::EquipmentChanged |
        player_actor_state_flag::EquipmentPresent;
    equipmentPatch.heroEquipment.transitionActionId = 7;
    equipmentPatch.transitionStartedAtSessionTimeMs = 1'000;
    equipmentPatch.transitionAnimationId = 22;
    equipmentPatch.transitionDurationMs = 1'000;
    equipmentPatch.attachmentNotifyOffsetMs = 200;

    PlayerActorStateMessage appearancePatch;
    appearancePatch.operation = PlayerActorStateOperation::ComponentDelta;
    appearancePatch.componentFlags =
        player_actor_state_flag::AppearanceChanged |
        player_actor_state_flag::AppearancePresent;

    // A normal state application treats the appearance-only revision as the
    // end of the equipment event, while publication coalescing must retain
    // the earlier unsent equipment patch and its timing metadata.
    const PlayerActorStateMessage applied =
        PlayerActorLifecycleReducer::MergeDelta(
            equipmentPatch, appearancePatch);
    require(applied.transitionStartedAtSessionTimeMs == SessionTimeUnset);
    require(applied.transitionAnimationId == 0);
    const PlayerActorStateMessage coalesced =
        PlayerActorLifecycleReducer::CoalesceDelta(
            equipmentPatch, appearancePatch);
    require((coalesced.componentFlags &
        player_actor_state_flag::EquipmentChanged) != 0);
    require(coalesced.transitionStartedAtSessionTimeMs == 1'000);
    require(coalesced.transitionAnimationId == 22);
    require(coalesced.transitionDurationMs == 1'000);
    require(coalesced.attachmentNotifyOffsetMs == 200);
    return failures;
}
