#include "EntityNetworkIdentityRegistry.h"

#include <cstdio>

namespace fable::multiplayer::entities
{
    void EntityNetworkIdentityRegistry::Initialize(
        const core::Diagnostics& diagnostics)
    {
        Clear();
        diagnostics_ = diagnostics;
    }

    bool EntityNetworkIdentityRegistry::Bind(
        std::uint64_t canonicalUid,
        std::uint64_t localUid)
    {
        if (canonicalUid == 0 || localUid == 0)
        {
            return false;
        }
        const auto local = localToCanonical_.find(localUid);
        const auto canonical = canonicalToLocal_.find(canonicalUid);
        if ((local != localToCanonical_.end() &&
                local->second != canonicalUid) ||
            (canonical != canonicalToLocal_.end() &&
                canonical->second != localUid))
        {
            char detail[256] = {};
            std::snprintf(
                detail,
                sizeof(detail),
                "canonical_uid=%016llX local_uid=%016llX",
                static_cast<unsigned long long>(canonicalUid),
                static_cast<unsigned long long>(localUid));
            diagnostics_.Event(
                "MultiplayerEntityIdentityConflict",
                detail);
            return false;
        }
        if (canonicalUid == localUid)
        {
            return true;
        }
        if (local == localToCanonical_.end() &&
            localToCanonical_.size() >= MaximumAliasCount)
        {
            diagnostics_.Event(
                "MultiplayerEntityIdentityOverflow",
                "bounded local-to-network Thing identity table is full");
            return false;
        }

        localToCanonical_[localUid] = canonicalUid;
        canonicalToLocal_[canonicalUid] = localUid;
        return true;
    }

    std::uint64_t EntityNetworkIdentityRegistry::Canonicalize(
        std::uint64_t localUid) const noexcept
    {
        const auto match = localToCanonical_.find(localUid);
        if (match != localToCanonical_.end())
        {
            return match->second;
        }
        // Directory and live-registry records already carry the canonical
        // value. Keep that value stable even when it is aliased to a distinct
        // process-local UID.
        if (canonicalToLocal_.find(localUid) != canonicalToLocal_.end())
        {
            return localUid;
        }
        return localUid;
    }

    std::uint64_t EntityNetworkIdentityRegistry::CanonicalizeLocalObservation(
        std::uint64_t localUid) const noexcept
    {
        const auto match = localToCanonical_.find(localUid);
        if (match != localToCanonical_.end())
        {
            return match->second;
        }
        const auto canonicalCollision = canonicalToLocal_.find(localUid);
        if (canonicalCollision != canonicalToLocal_.end() &&
            canonicalCollision->second != localUid)
        {
            return 0;
        }
        return localUid;
    }

    std::uint64_t EntityNetworkIdentityRegistry::FindLocal(
        std::uint64_t canonicalUid) const noexcept
    {
        const auto match = canonicalToLocal_.find(canonicalUid);
        return match != canonicalToLocal_.end()
            ? match->second
            : 0;
    }

    void EntityNetworkIdentityRegistry::ForgetLocal(
        std::uint64_t localUid) noexcept
    {
        const auto local = localToCanonical_.find(localUid);
        if (local == localToCanonical_.end())
        {
            return;
        }
        canonicalToLocal_.erase(local->second);
        localToCanonical_.erase(local);
    }

    void EntityNetworkIdentityRegistry::ForgetCanonical(
        std::uint64_t canonicalUid) noexcept
    {
        const auto canonical = canonicalToLocal_.find(canonicalUid);
        if (canonical == canonicalToLocal_.end())
        {
            return;
        }
        localToCanonical_.erase(canonical->second);
        canonicalToLocal_.erase(canonical);
    }

    std::size_t EntityNetworkIdentityRegistry::Size() const noexcept
    {
        return localToCanonical_.size();
    }

    void EntityNetworkIdentityRegistry::Clear() noexcept
    {
        localToCanonical_.clear();
        canonicalToLocal_.clear();
        diagnostics_ = {};
    }
}
