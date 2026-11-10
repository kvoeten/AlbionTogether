#pragma once

#include <array>
#include <cstddef>

namespace fable::game::creature::actions
{
    // Captures the first direct native SubmitAction reached by each synchronous
    // semantic submission scope. Nested submissions have a greater depth and
    // later sibling submissions cannot overwrite the requested result.
    class CreatureActionSubmissionReceiptStack final
    {
    public:
        static constexpr std::size_t Capacity = 8;

        bool Begin(void* creature, unsigned int nativeSubmitDepth) noexcept
        {
            if (creature == nullptr || size_ == receipts_.size())
            {
                return false;
            }
            receipts_[size_++] = {
                creature, nativeSubmitDepth, false, false};
            return true;
        }

        void Record(
            void* creature,
            unsigned int nativeSubmitDepth,
            bool accepted) noexcept
        {
            if (size_ == 0)
            {
                return;
            }
            Receipt& receipt = receipts_[size_ - 1];
            if (!receipt.observed && receipt.creature == creature &&
                nativeSubmitDepth == receipt.nativeSubmitDepth + 1)
            {
                receipt.observed = true;
                receipt.accepted = accepted;
            }
        }

        bool End(void* creature, bool& accepted) noexcept
        {
            accepted = false;
            if (size_ == 0 || receipts_[size_ - 1].creature != creature)
            {
                return false;
            }
            const Receipt receipt = receipts_[--size_];
            receipts_[size_] = {};
            accepted = receipt.accepted;
            return receipt.observed;
        }

    private:
        struct Receipt final
        {
            void* creature = nullptr;
            unsigned int nativeSubmitDepth = 0;
            bool observed = false;
            bool accepted = false;
        };

        std::array<Receipt, Capacity> receipts_ = {};
        std::size_t size_ = 0;
    };
}
