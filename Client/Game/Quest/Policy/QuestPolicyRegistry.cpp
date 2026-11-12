#include "QuestPolicyRegistry.h"

#include <cstring>

namespace
{
    using namespace fable::game::quest::policy;

    constexpr std::uint64_t RetailNameFingerprintXor =
        0x779A9F2EB8BAF2CDull;
    constexpr std::uint64_t RetailNameFingerprintSum =
        0x6D6F9DF0A771B36Full;

    std::uint64_t HashName(const char* const name) noexcept
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (const unsigned char* character =
                 reinterpret_cast<const unsigned char*>(name);
             *character != 0; ++character)
        {
            hash ^= *character;
            hash *= 1099511628211ull;
        }
        return hash;
    }

#define FABLE_QUEST_POLICY(                                                \
    questName, lifecyclePolicy, worldPolicy, decisionPolicy,               \
    transitionPolicy, rewardPolicy, confidencePolicy)                      \
    {                                                                       \
        questName, Lifecycle::lifecyclePolicy, World::worldPolicy,          \
        Decision::decisionPolicy, Transition::transitionPolicy,             \
        Reward::rewardPolicy, Confidence::confidencePolicy                  \
    },

    constexpr QuestPolicy Policies[] = {
#include "Game/Quest/Policy/Catalog/QuestPolicyCatalogEarly.inl"
#include "Game/Quest/Policy/Catalog/QuestPolicyCatalogStory.inl"
#include "Game/Quest/Policy/Catalog/QuestPolicyCatalogVignettes.inl"
    };
    static_assert(sizeof(Policies) / sizeof(Policies[0]) == 200,
        "the retail AddQuest catalog must remain exhaustive");

#undef FABLE_QUEST_POLICY

    constexpr QuestPolicy UnknownPolicy = {
        "<unknown>", Lifecycle::Blocked, World::None, Decision::None,
        Transition::None, Reward::None, Confidence::NeedsTrace};
}

namespace fable::game::quest::policy
{
    const QuestPolicy* QuestPolicyRegistry::Find(
        const char* const questName) noexcept
    {
        if (questName == nullptr || questName[0] == '\0')
        {
            return nullptr;
        }
        for (const QuestPolicy& policy : Policies)
        {
            if (std::strcmp(policy.name, questName) == 0)
            {
                return &policy;
            }
        }
        return nullptr;
    }

    const QuestPolicy& QuestPolicyRegistry::Resolve(
        const char* const questName) noexcept
    {
        const QuestPolicy* const policy = Find(questName);
        return policy != nullptr ? *policy : UnknownPolicy;
    }

    std::size_t QuestPolicyRegistry::Count() noexcept
    {
        return sizeof(Policies) / sizeof(Policies[0]);
    }

    bool QuestPolicyRegistry::IsCompleteAndUnique() noexcept
    {
        std::uint64_t fingerprintXor = 0;
        std::uint64_t fingerprintSum = 0;
        for (std::size_t left = 0; left < Count(); ++left)
        {
            if (Policies[left].name == nullptr ||
                Policies[left].name[0] == '\0')
            {
                return false;
            }
            const std::uint64_t nameHash = HashName(Policies[left].name);
            fingerprintXor ^= nameHash;
            fingerprintSum += nameHash;
            for (std::size_t right = left + 1; right < Count(); ++right)
            {
                if (std::strcmp(Policies[left].name,
                        Policies[right].name) == 0)
                {
                    return false;
                }
            }
        }
        // Order-independent fingerprints bind the checked-in policies to the
        // exact 200-name AddQuest set shipped by the supported Steam build.
        return fingerprintXor == RetailNameFingerprintXor &&
            fingerprintSum == RetailNameFingerprintSum;
    }
}
