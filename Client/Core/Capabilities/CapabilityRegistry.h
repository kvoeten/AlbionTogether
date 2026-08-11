#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace fable::core
{
    enum class CapabilityStatus : int
    {
        Unavailable = 0,
        Experimental = 1,
        Verified = 2,
    };

    class CapabilityRegistry final
    {
    public:
        void Set(
            const std::string& name,
            CapabilityStatus status,
            const std::string& detail = {});
        [[nodiscard]] CapabilityStatus Status(const std::string& name) const;
        [[nodiscard]] bool IsAvailable(const std::string& name) const;
        [[nodiscard]] bool IsVerified(const std::string& name) const;
        [[nodiscard]] std::string Describe(const std::string& name) const;
        [[nodiscard]] std::vector<std::string> Names() const;

    private:
        struct Entry
        {
            CapabilityStatus status = CapabilityStatus::Unavailable;
            std::string detail;
        };

        std::unordered_map<std::string, Entry> entries_;
    };
}
