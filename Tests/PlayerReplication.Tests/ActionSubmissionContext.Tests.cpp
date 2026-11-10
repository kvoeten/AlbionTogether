#include "Game/Creature/Actions/CreatureActionSubmissionContext.h"
#include "Game/Creature/Actions/CreatureActionSubmissionReceipt.h"

#include <cstdint>
#include <iostream>

int RunActionSubmissionContextTests()
{
    using fable::game::creature::actions::ActionSubmissionOrigin;
    using fable::game::creature::actions::ScopedActionSubmissionContext;
    using fable::game::creature::actions::ScopedActionSubmissionOrigin;
    using fable::game::creature::actions::
        CreatureActionSubmissionReceiptStack;

    int failures = 0;
    const auto check = [&failures](
        const bool condition,
        const char* const expression)
    {
        if (!condition)
        {
            std::cerr << "action submission context: failed: "
                      << expression << '\n';
            ++failures;
        }
    };

    int firstCreature = 0;
    int secondCreature = 0;
    const std::uint64_t firstId =
        ScopedActionSubmissionContext::NextCorrelationId();
    const std::uint64_t secondId =
        ScopedActionSubmissionContext::NextCorrelationId();
    check(firstId != 0, "first ID is nonzero");
    check(secondId != 0 && secondId != firstId, "IDs are unique");
    check(
        ScopedActionSubmissionContext::CurrentCorrelationId(
            &firstCreature) == 0,
        "no context exists before scope");

    {
        const ScopedActionSubmissionContext first(firstId, &firstCreature);
        (void)first;
        check(
            ScopedActionSubmissionContext::CurrentCorrelationId(
                &firstCreature) == firstId,
            "scope binds its creature");
        check(
            ScopedActionSubmissionContext::CurrentCorrelationId(
                &secondCreature) == 0,
            "scope cannot leak to another creature");

        {
            const ScopedActionSubmissionContext second(
                secondId, &secondCreature);
            (void)second;
            check(
                ScopedActionSubmissionContext::CurrentCorrelationId(
                    &secondCreature) == secondId,
                "nested scope binds the nested creature");
            check(
                ScopedActionSubmissionContext::CurrentCorrelationId(
                    &firstCreature) == 0,
                "nested scope hides the outer creature");
        }

        check(
            ScopedActionSubmissionContext::CurrentCorrelationId(
                &firstCreature) == firstId,
            "nested scope restores the outer context");
    }

    check(
        ScopedActionSubmissionContext::CurrentCorrelationId(
            &firstCreature) == 0,
        "scope clears on destruction");

    check(
        ScopedActionSubmissionOrigin::Current() ==
            ActionSubmissionOrigin::Retail,
        "submission origin defaults to retail");
    {
        const ScopedActionSubmissionOrigin outer(
            ActionSubmissionOrigin::ReplicatedPresentation);
        (void)outer;
        check(
            ScopedActionSubmissionOrigin::Current() ==
                ActionSubmissionOrigin::ReplicatedPresentation,
            "replicated presentation scope is visible");
        {
            const ScopedActionSubmissionOrigin nested(
                ActionSubmissionOrigin::ReplicatedPresentation);
            (void)nested;
            check(
                ScopedActionSubmissionOrigin::Current() ==
                    ActionSubmissionOrigin::ReplicatedPresentation,
                "nested replicated presentation scope remains active");
        }
        check(
            ScopedActionSubmissionOrigin::Current() ==
                ActionSubmissionOrigin::ReplicatedPresentation,
            "nested origin scope restores the outer origin");
    }
    check(
        ScopedActionSubmissionOrigin::Current() ==
            ActionSubmissionOrigin::Retail,
        "submission origin restores to retail");

    CreatureActionSubmissionReceiptStack receipts;
    bool accepted = true;
    check(receipts.Begin(&firstCreature, 0), "outer receipt begins");
    check(receipts.Begin(&firstCreature, 1), "nested receipt begins");
    receipts.Record(&firstCreature, 2, true);
    check(
        receipts.End(&firstCreature, accepted) && accepted,
        "nested accepted request reports its own result");
    receipts.Record(&firstCreature, 1, false);
    check(
        receipts.End(&firstCreature, accepted) && !accepted,
        "nested accepted request cannot satisfy rejected outer request");

    check(receipts.Begin(&firstCreature, 0), "sequential receipt begins");
    receipts.Record(&firstCreature, 1, false);
    receipts.Record(&firstCreature, 1, true);
    check(
        receipts.End(&firstCreature, accepted) && !accepted,
        "later sibling submission cannot overwrite the requested result");

    check(receipts.Begin(&firstCreature, 0), "receipt begins for accepted outer");
    check(receipts.Begin(&firstCreature, 1), "nested receipt begins");
    receipts.Record(&firstCreature, 2, false);
    check(
        receipts.End(&firstCreature, accepted) && !accepted,
        "nested rejected submission has its own result");
    receipts.Record(&firstCreature, 1, true);
    check(
        receipts.End(&firstCreature, accepted) && accepted,
        "outer accepted result is restored after nested receipt");

    check(receipts.Begin(&firstCreature, 0), "empty receipt begins");
    receipts.Record(&secondCreature, 1, true);
    receipts.Record(&firstCreature, 2, true);
    check(
        !receipts.End(&firstCreature, accepted) && !accepted,
        "wrong creature and nested depth cannot falsely accept request");

    return failures;
}
