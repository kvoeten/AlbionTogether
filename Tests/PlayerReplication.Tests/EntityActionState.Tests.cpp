#include "Multiplayer/Replication/EntityActions/EntityActionState.h"

#include <cstdint>
#include <iostream>

int RunEntityActionStateTests()
{
    using fable::multiplayer::replication::entity_actions::ActiveAction;

    int failures = 0;
    const auto check = [&failures](
        const bool condition,
        const char* const expression)
    {
        if (!condition)
        {
            std::cerr << "entity action state: failed: "
                      << expression << '\n';
            ++failures;
        }
    };

    ActiveAction first;
    first.localOrigin = true;
    first.nativeAbility = true;
    first.entityUid = 0x1111;
    first.actionCorrelationId = 101;

    ActiveAction second = first;
    second.actionCorrelationId = 102;

    check(
        first.MatchesAbilitySubmission(101, 0x2222),
        "exact correlation permits an explicitly routed native owner");
    check(
        !first.MatchesAbilitySubmission(102, first.entityUid),
        "a later ability cannot bind to the earlier request");
    check(
        second.MatchesAbilitySubmission(102, first.entityUid),
        "back-to-back ability requests retain independent identities");

    first.actionCorrelationId = 0;
    check(
        first.MatchesAbilitySubmission(0, first.entityUid),
        "legacy producers fall back to the same entity");
    check(
        !first.MatchesAbilitySubmission(0, 0x2222),
        "legacy fallback cannot cross entity ownership");

    first.nativeAction = &first;
    check(
        !first.MatchesAbilitySubmission(0, first.entityUid),
        "an already bound action cannot bind twice");
    return failures;
}
