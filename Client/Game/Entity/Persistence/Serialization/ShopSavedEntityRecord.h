#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fable::game::entity::persistence::serialization
{
    // A stable identity for one CTCShop component in one saved-entity map.
    // The component bytes are copied opaquely from the retail cell; no native
    // pointers or guessed field encoding cross this boundary.
    struct ShopSavedEntityIdentity final
    {
        std::uint32_t mapId = 0;
        std::uint64_t uid = 0;
        std::string scope;
        // Current Anniversary calls this first field a record/class name;
        // it is not a script-name pointer or an inferred script identity.
        std::string recordType;
        std::string definitionName;

        friend bool operator==(
            const ShopSavedEntityIdentity& left,
            const ShopSavedEntityIdentity& right) noexcept
        {
            return left.mapId == right.mapId && left.uid == right.uid &&
                left.scope == right.scope &&
                left.recordType == right.recordType &&
                left.definitionName == right.definitionName;
        }

        friend bool operator<(
            const ShopSavedEntityIdentity& left,
            const ShopSavedEntityIdentity& right) noexcept
        {
            if (left.mapId != right.mapId) return left.mapId < right.mapId;
            if (left.uid != right.uid) return left.uid < right.uid;
            if (left.scope != right.scope) return left.scope < right.scope;
            if (left.recordType != right.recordType)
                return left.recordType < right.recordType;
            return left.definitionName < right.definitionName;
        }
    };

    struct ShopSavedEntityRecord final
    {
        ShopSavedEntityIdentity identity;
        // Complete CTCShop frame: name, native framing, data, separator.
        std::vector<std::uint8_t> componentBytes;
    };

    // Extracts every CTCShop frame from valid entity records in one inflated
    // SAVED_ENTITIES cell. The map/campaign scope is supplied by the caller.
    bool ExtractShopSavedEntityRecords(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        std::uint32_t mapId,
        std::string_view scope,
        std::vector<ShopSavedEntityRecord>& result) noexcept;

    // Replaces only matching CTCShop component frames in a host cell. Host
    // entity headers, references, base fields, all other components, and
    // host-only entities remain byte-for-byte unchanged. Local-only entities
    // are never inserted. The supplied local batch must belong to mapId/scope;
    // a mismatched or malformed batch returns false with an empty result.
    bool SpliceShopSavedEntityRecords(
        const std::uint8_t* hostBytes,
        std::size_t hostByteCount,
        std::uint32_t mapId,
        std::string_view scope,
        const std::vector<ShopSavedEntityRecord>& localRecords,
        std::vector<std::uint8_t>& result) noexcept;
}
