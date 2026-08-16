#pragma once

#include "Core/Diagnostics/Diagnostics.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace fable::multiplayer::entities
{
    // Keeps Fable's process-local Thing UID separate from the host-issued
    // canonical network UID. This avoids mutating the retail Thing manager's
    // UID indexes when a peer has to reconstruct an entity locally.
    class EntityNetworkIdentityRegistry final
    {
    public:
        void Initialize(const core::Diagnostics& diagnostics);
        bool Bind(std::uint64_t canonicalUid, std::uint64_t localUid);
        // Accepts either a process-local UID or a canonical UID. Canonical
        // directory/registry records remain stable after an alias is bound.
        [[nodiscard]] std::uint64_t Canonicalize(
            std::uint64_t localUid) const noexcept;
        // Native callbacks report process-local UIDs. Unlike Canonicalize,
        // this rejects a previously assigned network UID when a different
        // local Thing happens to reuse that numeric value.
        [[nodiscard]] std::uint64_t CanonicalizeLocalObservation(
            std::uint64_t localUid) const noexcept;
        [[nodiscard]] std::uint64_t FindLocal(
            std::uint64_t canonicalUid) const noexcept;
        void ForgetLocal(std::uint64_t localUid) noexcept;
        void ForgetCanonical(std::uint64_t canonicalUid) noexcept;
        [[nodiscard]] std::size_t Size() const noexcept;
        void Clear() noexcept;

    private:
        static constexpr std::size_t MaximumAliasCount = 8192;

        core::Diagnostics diagnostics_ = {};
        std::unordered_map<std::uint64_t, std::uint64_t> localToCanonical_;
        std::unordered_map<std::uint64_t, std::uint64_t> canonicalToLocal_;
    };
}
