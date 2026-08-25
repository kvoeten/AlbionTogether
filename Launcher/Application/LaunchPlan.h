#pragma once

#include "../Configuration/Options.h"

#include <filesystem>
#include <string>
#include <vector>

namespace fable::launcher::application
{
    enum class LaunchMode
    {
        Normal,
        Automation,
        TransformProbe,
        LocalInstance,
        DualInstance,
        Multiplayer
    };

    struct LaunchPlan
    {
        Options options;
        LaunchMode mode = LaunchMode::Normal;
        std::filesystem::path launcherDirectory;
        std::filesystem::path executable;
        std::filesystem::path clientDll;
        std::filesystem::path artifactRoot;
        std::filesystem::path instanceRoot;
        std::filesystem::path eventPath;
        std::filesystem::path clientLog;
        std::filesystem::path fixtureDocumentsSource;
        std::filesystem::path fixtureDocuments;
        std::filesystem::path characterSnapshotSource;
        std::filesystem::path characterSnapshot;
        std::filesystem::path scriptData;
        std::wstring runId;
        std::wstring localSession;
        std::wstring clientMode;
        bool loadFixtureScenario = false;
    };

    LaunchPlan BuildLaunchPlan(
        const Options& options,
        const std::filesystem::path& launcherDirectory);

    std::vector<std::wstring> BuildGameArguments(const LaunchPlan& plan);

    void PrintPlanSummary(const LaunchPlan& plan);

    bool ValidateLaunchPlan(const LaunchPlan& plan);

    bool PrepareLaunchArtifacts(const LaunchPlan& plan);
}
