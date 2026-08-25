#include "EventLog.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace fable::launcher::diagnostics
{
    std::string ReadEventFile(const std::filesystem::path& eventPath)
    {
        std::ifstream stream(eventPath, std::ios::binary);
        if (!stream)
        {
            return {};
        }
        return std::string(
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>());
    }

    bool EventWasReported(const std::string& content, const char* state)
    {
        const std::string marker = std::string("\"state\":\"") + state + "\"";
        return content.find(marker) != std::string::npos;
    }

    std::size_t EventCount(const std::string& content, const char* state)
    {
        const std::string marker = std::string("\"state\":\"") + state + "\"";
        std::size_t count = 0;
        std::size_t position = 0;
        while ((position = content.find(marker, position)) != std::string::npos)
        {
            ++count;
            position += marker.size();
        }
        return count;
    }

    bool EventDetailContains(
        const std::string& content,
        const char* state,
        const char* detail)
    {
        const std::string stateMarker =
            std::string("\"state\":\"") + state + "\"";
        std::size_t position = 0;
        while ((position = content.find(stateMarker, position)) !=
            std::string::npos)
        {
            const std::size_t end = content.find('\n', position);
            const std::size_t length = end == std::string::npos
                ? std::string::npos
                : end - position;
            if (content.substr(position, length).find(detail) !=
                std::string::npos)
            {
                return true;
            }
            position += stateMarker.size();
        }
        return false;
    }

    std::size_t EventDetailCount(
        const std::string& content,
        const char* state,
        const char* detail)
    {
        const std::string stateMarker =
            std::string("\"state\":\"") + state + "\"";
        std::size_t count = 0;
        std::size_t position = 0;
        while ((position = content.find(stateMarker, position)) !=
            std::string::npos)
        {
            const std::size_t end = content.find('\n', position);
            const std::size_t length = end == std::string::npos
                ? std::string::npos
                : end - position;
            if (content.substr(position, length).find(detail) !=
                std::string::npos)
            {
                ++count;
            }
            position += stateMarker.size();
        }
        return count;
    }

    bool LastEventDetailContains(
        const std::string& content,
        const char* state,
        const char* detail)
    {
        const std::string stateMarker =
            std::string("\"state\":\"") + state + "\"";
        const std::size_t position = content.rfind(stateMarker);
        if (position == std::string::npos)
        {
            return false;
        }
        const std::size_t end = content.find('\n', position);
        const std::size_t length = end == std::string::npos
            ? std::string::npos
            : end - position;
        return content.substr(position, length).find(detail) !=
            std::string::npos;
    }

    std::uint64_t StablePlayerActorId(
        const std::wstring& role,
        const std::wstring& playerId)
    {
        const int required = WideCharToMultiByte(
            CP_UTF8, 0, playerId.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1)
        {
            return 0;
        }
        std::string utf8(static_cast<std::size_t>(required), '\0');
        WideCharToMultiByte(
            CP_UTF8, 0, playerId.c_str(), -1, utf8.data(), required,
            nullptr, nullptr);
        utf8.pop_back();
        std::uint64_t hash = 14695981039346656037ull;
        for (const unsigned char character : utf8)
        {
            hash ^= character;
            hash *= 1099511628211ull;
        }
        hash ^= role == L"host" ? 1u : 2u;
        hash *= 1099511628211ull;
        return hash == 0 ? 1 : hash;
    }

    std::string PvpReactionDetail(
        std::uint64_t sourceActorId,
        std::uint64_t targetActorId)
    {
        char detail[192] = {};
        std::snprintf(
            detail,
            sizeof(detail),
            "source_kind=1 source=%016llX target_kind=1 target=%016llX reaction_route=observer-replay",
            static_cast<unsigned long long>(sourceActorId),
            static_cast<unsigned long long>(targetActorId));
        return detail;
    }

    bool ReplicatedMovementWasApplied(
        const std::string& content,
        std::uint64_t actorId)
    {
        const std::string actorMarker = actorId == 0
            ? std::string()
            : "actor_id=" + std::to_string(actorId);
        const std::array<const char*, 2> markers = {
            "\"state\":\"CreatureMovementFacingRouted\"",
            "\"state\":\"CreatureBackgroundReplicatedMovementDriven\""};
        for (const char* const marker : markers)
        {
            std::size_t position = 0;
            while ((position = content.find(marker, position)) !=
                std::string::npos)
            {
                const std::size_t end = content.find('\n', position);
                const std::string event = content.substr(
                    position,
                    end == std::string::npos
                        ? std::string::npos
                        : end - position);
                const std::size_t motion = event.find("linear_velocity=(");
                if ((actorMarker.empty() ||
                        event.find(actorMarker) != std::string::npos) &&
                    motion != std::string::npos &&
                    event.find(
                        "linear_velocity=(0.000000,0.000000,0.000000)",
                        motion) == std::string::npos)
                {
                    return true;
                }
                position += std::strlen(marker);
            }
        }
        return false;
    }
}
