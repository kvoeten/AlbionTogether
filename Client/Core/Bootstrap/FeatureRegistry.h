#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fable::core::bootstrap
{
    enum class FeaturePhase : std::uint8_t
    {
        Process = 0,
        Runtime = 1,
        GameThread = 2,
        Multiplayer = 3,
        Automation = 4,
    };

    struct FeatureContext final
    {
        void* userData = nullptr;
    };

    using FeatureEnabledPredicate = bool (*)(const FeatureContext&) noexcept;
    using FeatureInstallCallback = bool (*)(FeatureContext&) noexcept;
    using FeatureUninstallCallback = void (*)(FeatureContext&) noexcept;

    // This type intentionally contains only pointers and scalar values. A descriptor is
    // emitted into the image at compile time and never owns runtime memory.
    struct FeatureDescriptor final
    {
        const char* stableId = nullptr;
        const char* name = nullptr;
        FeaturePhase phase = FeaturePhase::Runtime;
        std::int32_t priority = 0;
        FeatureEnabledPredicate enabled = nullptr;
        const char* const* dependencies = nullptr;
        std::size_t dependencyCount = 0;
        FeatureInstallCallback install = nullptr;
        FeatureUninstallCallback uninstall = nullptr;
        const char* failureIdentity = nullptr;
    };

    struct LinkerFeatureRange final
    {
        const FeatureDescriptor* begin = nullptr;
        const FeatureDescriptor* end = nullptr;
    };

    enum class FeaturePlanIssueCode : std::uint8_t
    {
        None = 0,
        InvalidRange,
        EmptyStableId,
        EmptyName,
        EmptyFailureIdentity,
        MissingInstallCallback,
        MissingUninstallCallback,
        InvalidPhase,
        DuplicateStableId,
        DuplicateName,
        MissingDependency,
        DisabledDependency,
        SelfDependency,
        DependencyOrderConflict,
        DependencyCycle,
    };

    struct FeaturePlanIssue final
    {
        FeaturePlanIssueCode code = FeaturePlanIssueCode::None;
        const char* featureId = nullptr;
        const char* relatedId = nullptr;
    };

    struct FeaturePlan final
    {
        std::vector<const FeatureDescriptor*> ordered;
        std::vector<FeaturePlanIssue> issues;

        void Clear() noexcept;
        [[nodiscard]] bool IsValid() const noexcept;
    };

    // Returns the linker range [.ft$M) bounded by the sentinels in FeatureRegistry.cpp.
    // Calling this function is read-only and does not perform registration or allocation.
    [[nodiscard]] LinkerFeatureRange GetLinkerFeatureRange() noexcept;

    // Builds a deterministic, dependency-safe plan. The caller owns the vectors in plan;
    // this routine does not retain descriptor pointers or context state.
    [[nodiscard]] bool BuildFeaturePlan(
        const FeatureDescriptor* begin,
        const FeatureDescriptor* end,
        const FeatureContext& context,
        FeaturePlan& plan);

    [[nodiscard]] inline bool BuildLinkerFeaturePlan(
        const FeatureContext& context,
        FeaturePlan& plan)
    {
        const LinkerFeatureRange range = GetLinkerFeatureRange();
        return BuildFeaturePlan(range.begin, range.end, context, plan);
    }

    struct FeatureInstallFailure final
    {
        const char* featureId = nullptr;
        const char* failureIdentity = nullptr;
    };

    // Owns only the successfully installed callbacks. It is intentionally separate from
    // FeaturePlan so a plan can be inspected, tested, and reused before installation.
    class FeatureInstallTransaction final
    {
    public:
        FeatureInstallTransaction() = default;
        FeatureInstallTransaction(const FeatureInstallTransaction&) = delete;
        FeatureInstallTransaction& operator=(const FeatureInstallTransaction&) = delete;
        ~FeatureInstallTransaction();

        [[nodiscard]] bool Install(
            const FeaturePlan& plan,
            FeatureContext& context,
            FeatureInstallFailure& failure);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInstalled() const noexcept { return installed_; }
        [[nodiscard]] std::size_t InstalledCount() const noexcept { return installedFeatures_.size(); }

    private:
        FeatureContext* context_ = nullptr;
        std::vector<const FeatureDescriptor*> installedFeatures_;
        bool installed_ = false;
    };
}

// Annotation-like declaration helpers. They emit immutable POD data into an MSVC
// linker section, with no static constructors or registration calls.
#if defined(_MSC_VER)
#pragma section(".ft$M", read)

#if defined(_WIN64)
#define FABLE_BOOTSTRAP_FORCE_INCLUDE(symbol) __pragma(comment(linker, "/include:" #symbol))
#else
#define FABLE_BOOTSTRAP_FORCE_INCLUDE(symbol) __pragma(comment(linker, "/include:_" #symbol))
#endif

#define FABLE_FEATURE_DEPENDENCIES(symbol, ...) \
    static constexpr const char* const symbol[] = {__VA_ARGS__};

#define FABLE_FEATURE_DESCRIPTOR( \
    symbol, stableIdValue, nameValue, phaseValue, priorityValue, enabledValue, dependenciesValue, \
    dependencyCountValue, installValue, uninstallValue, failureIdentityValue) \
    extern "C" __declspec(selectany) __declspec(allocate(".ft$M")) extern \
        const ::fable::core::bootstrap::FeatureDescriptor symbol = { \
            stableIdValue, nameValue, phaseValue, priorityValue, enabledValue, dependenciesValue, \
            dependencyCountValue, installValue, uninstallValue, failureIdentityValue}; \
    FABLE_BOOTSTRAP_FORCE_INCLUDE(symbol)
#else
#define FABLE_FEATURE_DEPENDENCIES(symbol, ...) \
    static constexpr const char* const symbol[] = {__VA_ARGS__};
#define FABLE_FEATURE_DESCRIPTOR( \
    symbol, stableIdValue, nameValue, phaseValue, priorityValue, enabledValue, dependenciesValue, \
    dependencyCountValue, installValue, uninstallValue, failureIdentityValue) \
    static constexpr ::fable::core::bootstrap::FeatureDescriptor symbol = { \
        stableIdValue, nameValue, phaseValue, priorityValue, enabledValue, dependenciesValue, \
        dependencyCountValue, installValue, uninstallValue, failureIdentityValue};
#endif
