#include "NetworkDiagnostics.h"

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "bcrypt.lib")

namespace fable::launcher::diagnostics
{
    namespace
    {
        constexpr std::uint32_t ProtocolMagic = 0x504D5446;
        constexpr std::uint16_t ProtocolVersion = 38;
        constexpr std::uint8_t PeerHelloType = 9;

#pragma pack(push, 1)
        struct WirePacketHeader final
        {
            std::uint32_t magic = ProtocolMagic;
            std::uint16_t version = ProtocolVersion;
            std::uint16_t size = 52;
            std::uint8_t type = PeerHelloType;
            std::uint8_t flags = 0;
            std::uint16_t payloadSize = 0;
            std::uint64_t sourceActorId = 0;
            std::uint64_t connectionNonce = 0;
            std::uint64_t streamId = 0;
            std::uint8_t streamKind = 0;
            std::uint8_t reserved[3] = {};
            std::uint64_t streamIncarnation = 0;
            std::uint32_t sequence = 0;
        };
#pragma pack(pop)

        static_assert(sizeof(WirePacketHeader) == 52);

        class Winsock final
        {
        public:
            Winsock()
            {
                WSADATA data = {};
                ready_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
            }

            ~Winsock()
            {
                if (ready_)
                {
                    WSACleanup();
                }
            }

            [[nodiscard]] bool Ready() const noexcept { return ready_; }

        private:
            bool ready_ = false;
        };

        std::uint64_t RandomNonzero()
        {
            std::uint64_t value = 0;
            if (BCryptGenRandom(
                    nullptr,
                    reinterpret_cast<PUCHAR>(&value),
                    sizeof(value),
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 || value == 0)
            {
                value = (static_cast<std::uint64_t>(GetTickCount64()) << 16) ^
                    GetCurrentProcessId();
            }
            return value == 0 ? 1 : value;
        }

        bool IsPrivateAddress(const IN_ADDR& address)
        {
            const std::uint32_t host = ntohl(address.S_un.S_addr);
            return (host & 0xFF000000u) == 0x0A000000u ||
                (host & 0xFFF00000u) == 0xAC100000u ||
                (host & 0xFFFF0000u) == 0xC0A80000u;
        }

        bool ResolveIpv4(
            const std::wstring& host,
            sockaddr_in& endpoint,
            std::wstring& display)
        {
            ADDRINFOW hints = {};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;
            hints.ai_protocol = IPPROTO_UDP;
            ADDRINFOW* addresses = nullptr;
            if (GetAddrInfoW(host.c_str(), nullptr, &hints, &addresses) != 0 ||
                addresses == nullptr)
            {
                return false;
            }
            endpoint = *reinterpret_cast<sockaddr_in*>(addresses->ai_addr);
            wchar_t text[INET_ADDRSTRLEN] = {};
            InetNtopW(AF_INET, &endpoint.sin_addr, text, std::size(text));
            display = text;
            FreeAddrInfoW(addresses);
            return true;
        }

        bool ProbeAlbionTogether(
            const sockaddr_in& endpoint,
            const unsigned long timeoutMilliseconds)
        {
            SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (socketHandle == INVALID_SOCKET)
            {
                return false;
            }
            const DWORD timeout = timeoutMilliseconds;
            setsockopt(
                socketHandle,
                SOL_SOCKET,
                SO_RCVTIMEO,
                reinterpret_cast<const char*>(&timeout),
                sizeof(timeout));

            WirePacketHeader hello;
            hello.sourceActorId = RandomNonzero();
            hello.connectionNonce = RandomNonzero();
            const int sent = sendto(
                socketHandle,
                reinterpret_cast<const char*>(&hello),
                sizeof(hello),
                0,
                reinterpret_cast<const sockaddr*>(&endpoint),
                sizeof(endpoint));
            if (sent != sizeof(hello))
            {
                closesocket(socketHandle);
                return false;
            }

            std::array<std::uint8_t, 1'200> response = {};
            sockaddr_in sender = {};
            int senderSize = sizeof(sender);
            const int received = recvfrom(
                socketHandle,
                reinterpret_cast<char*>(response.data()),
                static_cast<int>(response.size()),
                0,
                reinterpret_cast<sockaddr*>(&sender),
                &senderSize);
            closesocket(socketHandle);
            if (received < static_cast<int>(sizeof(WirePacketHeader)))
            {
                return false;
            }
            WirePacketHeader header;
            std::memcpy(&header, response.data(), sizeof(header));
            return header.magic == ProtocolMagic &&
                header.version == ProtocolVersion &&
                header.type == PeerHelloType &&
                header.size == received &&
                header.payloadSize == received - sizeof(WirePacketHeader) &&
                header.payloadSize == 16 &&
                header.sourceActorId != 0 && header.connectionNonce != 0;
        }

        bool ProbeIcmp(
            const IN_ADDR& address,
            const unsigned long timeoutMilliseconds)
        {
            HANDLE icmp = IcmpCreateFile();
            if (icmp == INVALID_HANDLE_VALUE)
            {
                return false;
            }
            constexpr char request[] = "AlbionTogether";
            std::array<std::uint8_t, sizeof(ICMP_ECHO_REPLY) + 64> reply = {};
            const DWORD responses = IcmpSendEcho(
                icmp,
                address.S_un.S_addr,
                const_cast<char*>(request),
                static_cast<WORD>(sizeof(request)),
                nullptr,
                reply.data(),
                static_cast<DWORD>(reply.size()),
                timeoutMilliseconds);
            IcmpCloseHandle(icmp);
            return responses != 0;
        }
    }

    std::vector<std::wstring> LocalIpv4Addresses()
    {
        std::vector<std::wstring> result;
        Winsock winsock;
        if (!winsock.Ready())
        {
            return result;
        }

        ULONG bytes = 16 * 1024;
        std::vector<std::uint8_t> storage(bytes);
        auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
        ULONG status = GetAdaptersAddresses(
            AF_INET,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                GAA_FLAG_SKIP_DNS_SERVER,
            nullptr,
            adapters,
            &bytes);
        if (status == ERROR_BUFFER_OVERFLOW)
        {
            storage.resize(bytes);
            adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
            status = GetAdaptersAddresses(
                AF_INET,
                GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                    GAA_FLAG_SKIP_DNS_SERVER,
                nullptr,
                adapters,
                &bytes);
        }
        if (status != NO_ERROR)
        {
            return result;
        }

        struct Address final
        {
            std::wstring text;
            bool privateAddress = false;
        };
        std::vector<Address> found;
        for (auto* adapter = adapters; adapter != nullptr;
             adapter = adapter->Next)
        {
            if (adapter->OperStatus != IfOperStatusUp ||
                adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
                adapter->IfType == IF_TYPE_TUNNEL)
            {
                continue;
            }
            for (auto* unicast = adapter->FirstUnicastAddress;
                 unicast != nullptr; unicast = unicast->Next)
            {
                if (unicast->Address.lpSockaddr == nullptr ||
                    unicast->Address.lpSockaddr->sa_family != AF_INET)
                {
                    continue;
                }
                const auto* address = reinterpret_cast<const sockaddr_in*>(
                    unicast->Address.lpSockaddr);
                const std::uint32_t host = ntohl(address->sin_addr.S_un.S_addr);
                if ((host & 0xFFFF0000u) == 0xA9FE0000u)
                {
                    continue;
                }
                wchar_t text[INET_ADDRSTRLEN] = {};
                if (InetNtopW(
                        AF_INET,
                        const_cast<IN_ADDR*>(&address->sin_addr),
                        text,
                        std::size(text)) != nullptr)
                {
                    found.push_back({text, IsPrivateAddress(address->sin_addr)});
                }
            }
        }
        std::stable_sort(
            found.begin(),
            found.end(),
            [](const Address& left, const Address& right)
            {
                return left.privateAddress && !right.privateAddress;
            });
        for (const Address& address : found)
        {
            if (std::find(result.begin(), result.end(), address.text) == result.end())
            {
                result.push_back(address.text);
            }
        }
        return result;
    }

    HostReachabilityResult TestHostReachability(
        const std::wstring& host,
        const unsigned short port,
        const unsigned long timeoutMilliseconds)
    {
        HostReachabilityResult result;
        if (host.empty())
        {
            result.state = HostReachabilityState::NotTested;
            result.detail = L"Enter the host IP address to test it.";
            return result;
        }
        Winsock winsock;
        if (!winsock.Ready())
        {
            result.state = HostReachabilityState::Error;
            result.detail = L"Windows networking could not be initialized.";
            return result;
        }

        sockaddr_in endpoint = {};
        if (!ResolveIpv4(host, endpoint, result.resolvedAddress))
        {
            result.state = HostReachabilityState::InvalidAddress;
            result.detail = L"The host address could not be resolved.";
            return result;
        }
        endpoint.sin_port = htons(port);
        if (ProbeAlbionTogether(endpoint, timeoutMilliseconds))
        {
            result.state = HostReachabilityState::AlbionTogetherDetected;
            result.detail = L"An AlbionTogether host answered on UDP port " +
                std::to_wstring(port) + L".";
            return result;
        }
        if (ProbeIcmp(endpoint.sin_addr, timeoutMilliseconds))
        {
            result.state = HostReachabilityState::AddressReachable;
            result.detail = L"The address answers, but no AlbionTogether host answered on UDP port " +
                std::to_wstring(port) + L".";
            return result;
        }
        result.state = HostReachabilityState::Unreachable;
        result.detail = L"No response was received. The host may be offline, behind CGNAT, or missing port forwarding.";
        return result;
    }
}
