#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <string>

namespace fable::launcher::diagnostics
{
    std::string ReadEventFile(const std::filesystem::path& eventPath);

    bool EventWasReported(const std::string& content, const char* state);

    std::size_t EventCount(const std::string& content, const char* state);

    bool EventDetailContains(
        const std::string& content,
        const char* state,
        const char* detail);

    std::size_t EventDetailCount(
        const std::string& content,
        const char* state,
        const char* detail);

    bool LastEventDetailContains(
        const std::string& content,
        const char* state,
        const char* detail);

    std::uint64_t StablePlayerActorId(
        const std::wstring& role,
        const std::wstring& playerId);

    std::string PvpReactionDetail(
        std::uint64_t sourceActorId,
        std::uint64_t targetActorId);

    bool ReplicatedMovementWasApplied(
        const std::string& content,
        std::uint64_t actorId = 0);
}
