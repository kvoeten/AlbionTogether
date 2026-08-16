#pragma once

#include <cstdint>

namespace fable::multiplayer::authority
{
    enum class MapBaselinePreparationResult : std::uint8_t
    {
        Ready,
        Deferred,
        Failed,
    };

    // Keeps persistence ordering out of the lease resolver. The host gate
    // submits the exact map baseline ahead of a Grant on the same reliable
    // lane; a guest accepts that Grant only after the revision is staged or
    // installed locally.
    class MapAuthorityBaselineGate
    {
    public:
        virtual ~MapAuthorityBaselineGate() = default;

        virtual MapBaselinePreparationResult PrepareHostGrant(
            std::uint16_t mapId,
            std::uint64_t& baselineRevision) = 0;
        [[nodiscard]] virtual bool IsGuestGrantReady(
            std::uint16_t mapId,
            std::uint64_t baselineRevision) const noexcept = 0;
    };
}
