#include "Game/Quest/Policy/QuestPolicyRegistry.h"

#include <cstring>
#include <iostream>

namespace
{
    int failures = 0;

    void Check(const bool condition, const char* const detail)
    {
        if (!condition)
        {
            ++failures;
            std::cerr << "quest policy registry: " << detail << '\n';
        }
    }
}

int RunQuestPolicyRegistryTests()
{
    using namespace fable::game::quest::policy;

    Check(QuestPolicyRegistry::Count() == 200,
        "every installed AddQuest registration has one policy");
    Check(QuestPolicyRegistry::IsCompleteAndUnique(),
        "registered policies are named and unique");

    const QuestPolicy& tutorial =
        QuestPolicyRegistry::Resolve("Q_GuildTrainingMelee");
    Check(tutorial.lifecycle == Lifecycle::LocalOnly,
        "private tutorial targets stay local to their Hero");

    const QuestPolicy& campaign =
        QuestPolicyRegistry::Resolve("Q_OrchardFarmRaid");
    Check(campaign.lifecycle == Lifecycle::Shared &&
            campaign.world == World::MapOwner,
        "campaign progression is shared while world simulation has one owner");

    const QuestPolicy& unknown =
        QuestPolicyRegistry::Resolve("Custom_Unreviewed_Quest");
    Check(unknown.lifecycle == Lifecycle::Blocked &&
            unknown.confidence == Confidence::NeedsTrace &&
            std::strcmp(unknown.name, "<unknown>") == 0,
        "unknown or modded quests fail closed");

    return failures;
}
