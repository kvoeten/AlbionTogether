#include "ConnectionNonceRegistry.h"

#include <Windows.h>
#include <bcrypt.h>

#include <atomic>

#pragma comment(lib, "bcrypt.lib")

namespace fable::multiplayer
{
    std::uint64_t ConnectionNonceRegistry::GenerateLocal() noexcept
    {
        std::uint64_t nonce = 0;
        if (BCryptGenRandom(
                nullptr,
                reinterpret_cast<PUCHAR>(&nonce),
                sizeof(nonce),
                BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 && nonce != 0)
        {
            return nonce;
        }
        static std::atomic_uint64_t fallback = 1;
        nonce = fallback.fetch_add(1, std::memory_order_relaxed);
        return nonce == 0 ? fallback.fetch_add(1, std::memory_order_relaxed) :
            nonce;
    }

}
