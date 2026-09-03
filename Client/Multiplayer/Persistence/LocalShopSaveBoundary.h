#pragma once

#include "Core/Diagnostics/Diagnostics.h"
#include "Game/Entity/Persistence/Native/SavedEntityCompression.h"
#include "Game/Entity/Persistence/SavedEntityMapBlobSnapshot.h"
#include "Game/Entity/Persistence/Serialization/ShopSavedEntityRecord.h"
#include "Multiplayer/Persistence/SavedEntityCollectionBaselineTransfer.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace fable::multiplayer::persistence
{
    // Captures only the local player's current CTCShop component frames and
    // overlays those frames onto an otherwise host-owned binary map record.
    // Native pointers, whole entities, and local-only merchants never cross
    // this boundary.
    class LocalShopSaveBoundary final
    {
    public:
        static constexpr std::size_t MaximumMerchants = 4'096;
        static constexpr std::size_t MaximumComponentBytes = 16 * 1024 * 1024;
        static constexpr std::uint32_t MaximumMapId = 4'095;

        bool Initialize(
            HMODULE gameModule,
            const core::Diagnostics& diagnostics) noexcept;
        void Shutdown() noexcept;

        // Starts a new selected-save capture. The scope prevents records from
        // a retired save/incarnation matching a later merchant by UID alone.
        void BeginGuestCollection() noexcept;
        void ObserveGuestRecord(
            const game::entity::persistence::SavedEntityMapBlobSnapshot&
                snapshot) noexcept;

        // Rewrites only matching CTCShop frames in record.bytes. Unsupported
        // formats and records without a local merchant are left unchanged.
        [[nodiscard]] bool RewriteHostRecord(
            SavedEntityCollectionRecord& record) noexcept;

        [[nodiscard]] std::uint64_t Revision() const noexcept
        {
            return revision_;
        }

        [[nodiscard]] std::size_t MerchantCount() const noexcept
        {
            return records_.size();
        }

    private:
        using Identity = game::entity::persistence::serialization::
            ShopSavedEntityIdentity;
        using ShopRecord = game::entity::persistence::serialization::
            ShopSavedEntityRecord;

        struct CellFingerprint final
        {
            std::uint64_t hash = 0;
            std::uint32_t metadata = 0;
            bool valid = false;
        };

        static std::uint64_t HashBytes(
            const std::uint8_t* bytes,
            std::size_t byteCount) noexcept;
        static bool SameRecord(
            const ShopRecord& left,
            const ShopRecord& right) noexcept;
        static bool IsSupportedSnapshot(
            const game::entity::persistence::SavedEntityMapBlobSnapshot&
                snapshot) noexcept;
        void ReportUnsupported(const char* reason) const noexcept;
        void AdvanceRevision() noexcept;

        game::entity::persistence::native::SavedEntityCompression compression_;
        std::map<Identity, ShopRecord> records_;
        std::map<std::uint32_t, CellFingerprint> fingerprints_;
        std::string scope_;
        core::Diagnostics diagnostics_ = {};
        std::uint64_t scopeGeneration_ = 0;
        std::uint64_t revision_ = 0;
        std::size_t componentBytes_ = 0;
        mutable unsigned int unsupportedDiagnosticCount_ = 0;
        bool captureActive_ = false;
    };
}
