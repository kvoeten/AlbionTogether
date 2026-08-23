#include "FeatureRegistry.h"

#include <algorithm>
#include <cstring>

namespace fable::core::bootstrap
{
    namespace
    {
        bool IsValidPhase(const FeaturePhase phase) noexcept
        {
            return static_cast<std::uint8_t>(phase) <= static_cast<std::uint8_t>(FeaturePhase::Automation);
        }

        int CompareKey(const FeatureDescriptor& left, const FeatureDescriptor& right) noexcept
        {
            const auto leftPhase = static_cast<std::uint8_t>(left.phase);
            const auto rightPhase = static_cast<std::uint8_t>(right.phase);
            if (leftPhase != rightPhase)
            {
                return leftPhase < rightPhase ? -1 : 1;
            }
            if (left.priority != right.priority)
            {
                return left.priority < right.priority ? -1 : 1;
            }
            const int nameComparison = std::strcmp(left.name, right.name);
            if (nameComparison != 0)
            {
                return nameComparison < 0 ? -1 : 1;
            }
            const int idComparison = std::strcmp(left.stableId, right.stableId);
            return idComparison < 0 ? -1 : idComparison > 0 ? 1 : 0;
        }

        const FeatureDescriptor* FindById(
            const std::vector<const FeatureDescriptor*>& descriptors,
            const char* stableId) noexcept
        {
            for (const FeatureDescriptor* descriptor : descriptors)
            {
                if (descriptor->stableId != nullptr && stableId != nullptr &&
                    std::strcmp(descriptor->stableId, stableId) == 0)
                {
                    return descriptor;
                }
            }
            return nullptr;
        }

        bool StringsEqual(const char* left, const char* right) noexcept
        {
            if (left == right)
            {
                return true;
            }
            if (left == nullptr || right == nullptr)
            {
                return false;
            }
            return std::strcmp(left, right) == 0;
        }

        bool ContainsIssue(
            const FeaturePlan& plan,
            const FeaturePlanIssueCode code,
            const char* featureId,
            const char* relatedId) noexcept
        {
            for (const FeaturePlanIssue& issue : plan.issues)
            {
                if (issue.code == code &&
                    StringsEqual(issue.featureId, featureId) &&
                    StringsEqual(issue.relatedId, relatedId))
                {
                    return true;
                }
            }
            return false;
        }

        void AddIssue(
            FeaturePlan& plan,
            const FeaturePlanIssueCode code,
            const char* featureId,
            const char* relatedId = nullptr) noexcept
        {
            if (!ContainsIssue(plan, code, featureId, relatedId))
            {
                plan.issues.push_back(FeaturePlanIssue{code, featureId, relatedId});
            }
        }
    }

#if defined(_MSC_VER)
#pragma section(".ft$A", read)
#pragma section(".ft$Z", read)
    extern "C" __declspec(allocate(".ft$A")) extern const FeatureDescriptor fableFeatureRangeBegin = {};
    extern "C" __declspec(allocate(".ft$Z")) extern const FeatureDescriptor fableFeatureRangeEnd = {};

#if defined(_WIN64)
#pragma comment(linker, "/include:fableFeatureRangeBegin")
#pragma comment(linker, "/include:fableFeatureRangeEnd")
#else
#pragma comment(linker, "/include:_fableFeatureRangeBegin")
#pragma comment(linker, "/include:_fableFeatureRangeEnd")
#endif
#endif

    LinkerFeatureRange GetLinkerFeatureRange() noexcept
    {
#if defined(_MSC_VER)
        return LinkerFeatureRange{&fableFeatureRangeBegin + 1, &fableFeatureRangeEnd};
#else
        return LinkerFeatureRange{};
#endif
    }

    void FeaturePlan::Clear() noexcept
    {
        ordered.clear();
        issues.clear();
    }

    bool FeaturePlan::IsValid() const noexcept
    {
        return issues.empty();
    }

    bool BuildFeaturePlan(
        const FeatureDescriptor* begin,
        const FeatureDescriptor* end,
        const FeatureContext& context,
        FeaturePlan& plan)
    {
        plan.Clear();
        if (begin == nullptr || end == nullptr || begin > end)
        {
            AddIssue(plan, FeaturePlanIssueCode::InvalidRange, nullptr, nullptr);
            return false;
        }

        std::vector<const FeatureDescriptor*> allDescriptors;
        for (const FeatureDescriptor* descriptor = begin; descriptor != end; ++descriptor)
        {
            allDescriptors.push_back(descriptor);
            if (descriptor->stableId == nullptr || descriptor->stableId[0] == '\0')
            {
                AddIssue(plan, FeaturePlanIssueCode::EmptyStableId, descriptor->stableId, nullptr);
            }
            if (descriptor->name == nullptr || descriptor->name[0] == '\0')
            {
                AddIssue(plan, FeaturePlanIssueCode::EmptyName, descriptor->stableId, nullptr);
            }
            if (descriptor->failureIdentity == nullptr || descriptor->failureIdentity[0] == '\0')
            {
                AddIssue(plan, FeaturePlanIssueCode::EmptyFailureIdentity, descriptor->stableId, nullptr);
            }
            if (descriptor->install == nullptr)
            {
                AddIssue(plan, FeaturePlanIssueCode::MissingInstallCallback, descriptor->stableId, nullptr);
            }
            if (descriptor->uninstall == nullptr)
            {
                AddIssue(plan, FeaturePlanIssueCode::MissingUninstallCallback, descriptor->stableId, nullptr);
            }
            if (!IsValidPhase(descriptor->phase))
            {
                AddIssue(plan, FeaturePlanIssueCode::InvalidPhase, descriptor->stableId, nullptr);
            }
            if (descriptor->dependencyCount != 0 && descriptor->dependencies == nullptr)
            {
                AddIssue(plan, FeaturePlanIssueCode::MissingDependency, descriptor->stableId, nullptr);
            }
        }

        for (std::size_t index = 0; index < allDescriptors.size(); ++index)
        {
            const FeatureDescriptor& descriptor = *allDescriptors[index];
            if (descriptor.stableId == nullptr || descriptor.name == nullptr)
            {
                continue;
            }
            for (std::size_t otherIndex = index + 1; otherIndex < allDescriptors.size(); ++otherIndex)
            {
                const FeatureDescriptor& other = *allDescriptors[otherIndex];
                if (other.stableId != nullptr && std::strcmp(descriptor.stableId, other.stableId) == 0)
                {
                    AddIssue(plan, FeaturePlanIssueCode::DuplicateStableId, descriptor.stableId, other.stableId);
                }
                if (other.name != nullptr && std::strcmp(descriptor.name, other.name) == 0)
                {
                    AddIssue(plan, FeaturePlanIssueCode::DuplicateName, descriptor.stableId, other.stableId);
                }
            }
        }

        for (const FeatureDescriptor* descriptor : allDescriptors)
        {
            if (descriptor->stableId == nullptr || descriptor->name == nullptr)
            {
                continue;
            }
            if (descriptor->enabled == nullptr || descriptor->enabled(context))
            {
                plan.ordered.push_back(descriptor);
            }
        }

        // Disabled features are intentionally excluded from the plan. An enabled feature
        // may not silently depend on one, because that produces a partially initialized graph.
        for (const FeatureDescriptor* descriptor : plan.ordered)
        {
            if (descriptor->dependencyCount != 0 && descriptor->dependencies == nullptr)
            {
                continue;
            }
            for (std::size_t dependencyIndex = 0; dependencyIndex < descriptor->dependencyCount; ++dependencyIndex)
            {
                const char* dependencyId = descriptor->dependencies[dependencyIndex];
                if (dependencyId == nullptr || dependencyId[0] == '\0')
                {
                    AddIssue(plan, FeaturePlanIssueCode::MissingDependency, descriptor->stableId, dependencyId);
                    continue;
                }
                const FeatureDescriptor* dependency = FindById(allDescriptors, dependencyId);
                if (dependency == nullptr)
                {
                    AddIssue(plan, FeaturePlanIssueCode::MissingDependency, descriptor->stableId, dependencyId);
                    continue;
                }
                if (dependency == descriptor)
                {
                    AddIssue(plan, FeaturePlanIssueCode::SelfDependency, descriptor->stableId, dependencyId);
                    continue;
                }
                if (std::find(plan.ordered.begin(), plan.ordered.end(), dependency) == plan.ordered.end())
                {
                    AddIssue(plan, FeaturePlanIssueCode::DisabledDependency, descriptor->stableId, dependencyId);
                    continue;
                }
                if (static_cast<std::uint8_t>(dependency->phase) >
                    static_cast<std::uint8_t>(descriptor->phase))
                {
                    AddIssue(plan, FeaturePlanIssueCode::DependencyOrderConflict, descriptor->stableId, dependencyId);
                }
            }
        }

        if (!plan.issues.empty())
        {
            plan.ordered.clear();
            return false;
        }

        std::vector<const FeatureDescriptor*> remaining = plan.ordered;
        plan.ordered.clear();
        while (!remaining.empty())
        {
            const FeatureDescriptor* next = nullptr;
            for (const FeatureDescriptor* candidate : remaining)
            {
                bool hasRemainingDependency = false;
                for (std::size_t dependencyIndex = 0; dependencyIndex < candidate->dependencyCount; ++dependencyIndex)
                {
                    const FeatureDescriptor* dependency = FindById(remaining, candidate->dependencies[dependencyIndex]);
                    if (dependency != nullptr)
                    {
                        hasRemainingDependency = true;
                        break;
                    }
                }
                if (!hasRemainingDependency && (next == nullptr || CompareKey(*candidate, *next) < 0))
                {
                    next = candidate;
                }
            }
            if (next == nullptr)
            {
                AddIssue(plan, FeaturePlanIssueCode::DependencyCycle, nullptr, nullptr);
                plan.ordered.clear();
                return false;
            }
            plan.ordered.push_back(next);
            remaining.erase(std::find(remaining.begin(), remaining.end(), next));
        }
        return true;
    }

    FeatureInstallTransaction::~FeatureInstallTransaction()
    {
        Shutdown();
    }

    bool FeatureInstallTransaction::Install(
        const FeaturePlan& plan,
        FeatureContext& context,
        FeatureInstallFailure& failure)
    {
        failure = FeatureInstallFailure{};
        if (installed_ || !plan.IsValid())
        {
            return false;
        }

        context_ = &context;
        installedFeatures_.reserve(plan.ordered.size());
        for (const FeatureDescriptor* descriptor : plan.ordered)
        {
            if (!descriptor->install(context))
            {
                failure = FeatureInstallFailure{descriptor->stableId, descriptor->failureIdentity};
                Shutdown();
                return false;
            }
            installedFeatures_.push_back(descriptor);
        }
        installed_ = true;
        return true;
    }

    void FeatureInstallTransaction::Shutdown() noexcept
    {
        if (context_ != nullptr)
        {
            for (auto iterator = installedFeatures_.rbegin(); iterator != installedFeatures_.rend(); ++iterator)
            {
                (*iterator)->uninstall(*context_);
            }
        }
        installedFeatures_.clear();
        context_ = nullptr;
        installed_ = false;
    }
}
