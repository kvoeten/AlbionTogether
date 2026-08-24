#pragma once

#include "Multiplayer/Combat/CombatActionLedger.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fable::multiplayer::combat
{
    class CombatTerminalTransitionState final
    {
    public:
        static constexpr std::size_t Capacity = 128;

        struct Progress final
        {
            bool healthHandled = false;
            bool reactionHandled = false;
            bool deathSubmitted = false;
        };

        [[nodiscard]] Progress Get(
            const CombatLifecycle& lifecycle) const noexcept
        {
            const Entry* const entry = Find(lifecycle);
            return entry != nullptr ? entry->progress : Progress{};
        }

        void MarkHealthHandled(const CombatLifecycle& lifecycle) noexcept
        {
            Ensure(lifecycle).progress.healthHandled = true;
        }

        void MarkReactionHandled(const CombatLifecycle& lifecycle) noexcept
        {
            Ensure(lifecycle).progress.reactionHandled = true;
        }

        void MarkDeathSubmitted(const CombatLifecycle& lifecycle) noexcept
        {
            Ensure(lifecycle).progress.deathSubmitted = true;
        }

        void Clear() noexcept
        {
            entries_ = {};
            nextSerial_ = 0;
        }

    private:
        struct Entry final
        {
            CombatLifecycle lifecycle;
            Progress progress;
            std::uint64_t serial = 0;
            bool occupied = false;
        };

        [[nodiscard]] const Entry* Find(
            const CombatLifecycle& lifecycle) const noexcept
        {
            for (const Entry& entry : entries_)
            {
                if (entry.occupied && entry.lifecycle == lifecycle)
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        [[nodiscard]] Entry& Ensure(
            const CombatLifecycle& lifecycle) noexcept
        {
            Entry* oldest = &entries_[0];
            for (Entry& entry : entries_)
            {
                if (entry.occupied && entry.lifecycle == lifecycle)
                {
                    entry.serial = ++nextSerial_;
                    return entry;
                }
                if (!entry.occupied)
                {
                    entry = {lifecycle, {}, ++nextSerial_, true};
                    return entry;
                }
                if (entry.serial < oldest->serial)
                {
                    oldest = &entry;
                }
            }
            *oldest = {lifecycle, {}, ++nextSerial_, true};
            return *oldest;
        }

        std::array<Entry, Capacity> entries_ = {};
        std::uint64_t nextSerial_ = 0;
    };
}
