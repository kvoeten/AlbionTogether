#include "CapabilityRegistry.h"

#include <algorithm>

namespace fable::core
{
    void CapabilityRegistry::Set(
        const std::string& name,
        CapabilityStatus status,
        const std::string& detail)
    {
        entries_[name] = Entry{status, detail};
    }

    CapabilityStatus CapabilityRegistry::Status(const std::string& name) const
    {
        const auto iterator = entries_.find(name);
        return iterator != entries_.end()
            ? iterator->second.status
            : CapabilityStatus::Unavailable;
    }

    bool CapabilityRegistry::IsAvailable(const std::string& name) const
    {
        return Status(name) != CapabilityStatus::Unavailable;
    }

    bool CapabilityRegistry::IsVerified(const std::string& name) const
    {
        return Status(name) == CapabilityStatus::Verified;
    }

    std::string CapabilityRegistry::Describe(const std::string& name) const
    {
        const auto iterator = entries_.find(name);
        return iterator != entries_.end() ? iterator->second.detail : std::string{};
    }

    std::vector<std::string> CapabilityRegistry::Names() const
    {
        std::vector<std::string> names;
        names.reserve(entries_.size());
        for (const auto& entry : entries_)
        {
            names.push_back(entry.first);
        }
        std::sort(names.begin(), names.end());
        return names;
    }
}
