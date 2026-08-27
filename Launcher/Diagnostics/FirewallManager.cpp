#include "FirewallManager.h"

#include <Windows.h>
#include <netfw.h>
#include <shellapi.h>

#include <filesystem>
#include <iterator>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shell32.lib")

namespace fable::launcher::diagnostics
{
    namespace
    {
        constexpr wchar_t RuleName[] = L"AlbionTogether Multiplayer";

        class ComApartment final
        {
        public:
            ComApartment()
                : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))
            {
            }

            ~ComApartment()
            {
                if (SUCCEEDED(result_))
                {
                    CoUninitialize();
                }
            }

            [[nodiscard]] bool Ready() const noexcept
            {
                return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
            }

        private:
            HRESULT result_;
        };

        std::wstring FormatHresult(const HRESULT result)
        {
            wchar_t* message = nullptr;
            const DWORD length = FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER |
                    FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                static_cast<DWORD>(result),
                0,
                reinterpret_cast<wchar_t*>(&message),
                0,
                nullptr);
            std::wstring value = length != 0 && message != nullptr
                ? std::wstring(message, length)
                : L"Windows error " + std::to_wstring(result);
            if (message != nullptr)
            {
                LocalFree(message);
            }
            while (!value.empty() &&
                (value.back() == L'\r' || value.back() == L'\n'))
            {
                value.pop_back();
            }
            return value;
        }

        bool CurrentFirewallEnabled(INetFwPolicy2* policy, bool& enabled)
        {
            enabled = false;
            long profiles = 0;
            if (FAILED(policy->get_CurrentProfileTypes(&profiles)))
            {
                return false;
            }
            constexpr NET_FW_PROFILE_TYPE2 profileTypes[] = {
                NET_FW_PROFILE2_DOMAIN,
                NET_FW_PROFILE2_PRIVATE,
                NET_FW_PROFILE2_PUBLIC};
            for (const NET_FW_PROFILE_TYPE2 profile : profileTypes)
            {
                if ((profiles & profile) == 0)
                {
                    continue;
                }
                VARIANT_BOOL profileEnabled = VARIANT_FALSE;
                if (FAILED(policy->get_FirewallEnabled(
                        profile, &profileEnabled)))
                {
                    return false;
                }
                enabled = enabled || profileEnabled == VARIANT_TRUE;
            }
            return true;
        }

        HRESULT OpenPolicy(INetFwPolicy2** policy)
        {
            return CoCreateInstance(
                __uuidof(NetFwPolicy2),
                nullptr,
                CLSCTX_INPROC_SERVER,
                __uuidof(INetFwPolicy2),
                reinterpret_cast<void**>(policy));
        }

        bool RuleMatches(INetFwRule* rule, const unsigned short port)
        {
            VARIANT_BOOL enabled = VARIANT_FALSE;
            long protocol = NET_FW_IP_PROTOCOL_ANY;
            NET_FW_RULE_DIRECTION direction = NET_FW_RULE_DIR_MAX;
            NET_FW_ACTION action = NET_FW_ACTION_BLOCK;
            BSTR localPorts = nullptr;
            const bool read =
                SUCCEEDED(rule->get_Enabled(&enabled)) &&
                SUCCEEDED(rule->get_Protocol(&protocol)) &&
                SUCCEEDED(rule->get_Direction(&direction)) &&
                SUCCEEDED(rule->get_Action(&action)) &&
                SUCCEEDED(rule->get_LocalPorts(&localPorts));
            const std::wstring expected = std::to_wstring(port);
            const bool matches = read && enabled == VARIANT_TRUE &&
                protocol == NET_FW_IP_PROTOCOL_UDP &&
                direction == NET_FW_RULE_DIR_IN &&
                action == NET_FW_ACTION_ALLOW && localPorts != nullptr &&
                expected == localPorts;
            if (localPorts != nullptr)
            {
                SysFreeString(localPorts);
            }
            return matches;
        }
    }

    FirewallResult CheckFirewall(const unsigned short port)
    {
        FirewallResult result;
        ComApartment apartment;
        if (!apartment.Ready())
        {
            result.detail = L"Windows Firewall could not be inspected.";
            return result;
        }

        INetFwPolicy2* policy = nullptr;
        HRESULT status = OpenPolicy(&policy);
        if (FAILED(status) || policy == nullptr)
        {
            result.detail = L"Windows Firewall could not be inspected: " +
                FormatHresult(status);
            return result;
        }

        bool enabled = false;
        if (!CurrentFirewallEnabled(policy, enabled))
        {
            policy->Release();
            result.detail = L"The active Windows Firewall profile could not be read.";
            return result;
        }
        if (!enabled)
        {
            policy->Release();
            result.state = FirewallState::FirewallDisabled;
            result.detail = L"Windows Firewall is disabled for the active profile.";
            return result;
        }

        INetFwRules* rules = nullptr;
        status = policy->get_Rules(&rules);
        policy->Release();
        if (FAILED(status) || rules == nullptr)
        {
            result.detail = L"Windows Firewall rules could not be read: " +
                FormatHresult(status);
            return result;
        }
        BSTR name = SysAllocString(RuleName);
        INetFwRule* rule = nullptr;
        status = name != nullptr ? rules->Item(name, &rule) : E_OUTOFMEMORY;
        if (name != nullptr)
        {
            SysFreeString(name);
        }
        rules->Release();

        if (SUCCEEDED(status) && rule != nullptr && RuleMatches(rule, port))
        {
            rule->Release();
            result.state = FirewallState::Allowed;
            result.detail = L"UDP port " + std::to_wstring(port) +
                L" is allowed through Windows Firewall.";
            return result;
        }
        if (rule != nullptr)
        {
            rule->Release();
        }
        result.state = FirewallState::RuleMissing;
        result.detail = L"UDP port " + std::to_wstring(port) +
            L" is not allowed through Windows Firewall.";
        return result;
    }

    bool InstallFirewallRule(
        const unsigned short port,
        std::wstring& error)
    {
        error.clear();
        ComApartment apartment;
        if (!apartment.Ready())
        {
            error = L"COM initialization failed.";
            return false;
        }

        INetFwPolicy2* policy = nullptr;
        HRESULT status = OpenPolicy(&policy);
        if (FAILED(status) || policy == nullptr)
        {
            error = FormatHresult(status);
            return false;
        }
        INetFwRules* rules = nullptr;
        status = policy->get_Rules(&rules);
        policy->Release();
        if (FAILED(status) || rules == nullptr)
        {
            error = FormatHresult(status);
            return false;
        }

        INetFwRule* rule = nullptr;
        status = CoCreateInstance(
            __uuidof(NetFwRule),
            nullptr,
            CLSCTX_INPROC_SERVER,
            __uuidof(INetFwRule),
            reinterpret_cast<void**>(&rule));
        if (FAILED(status) || rule == nullptr)
        {
            rules->Release();
            error = FormatHresult(status);
            return false;
        }

        BSTR name = SysAllocString(RuleName);
        BSTR description = SysAllocString(
            L"Allows AlbionTogether multiplayer traffic.");
        const std::wstring portText = std::to_wstring(port);
        BSTR ports = SysAllocString(portText.c_str());
        const bool configured = name != nullptr && description != nullptr &&
            ports != nullptr &&
            SUCCEEDED(rule->put_Name(name)) &&
            SUCCEEDED(rule->put_Description(description)) &&
            SUCCEEDED(rule->put_Protocol(NET_FW_IP_PROTOCOL_UDP)) &&
            SUCCEEDED(rule->put_LocalPorts(ports)) &&
            SUCCEEDED(rule->put_Direction(NET_FW_RULE_DIR_IN)) &&
            SUCCEEDED(rule->put_Action(NET_FW_ACTION_ALLOW)) &&
            SUCCEEDED(rule->put_Profiles(NET_FW_PROFILE2_ALL)) &&
            SUCCEEDED(rule->put_Enabled(VARIANT_TRUE));
        if (name != nullptr) SysFreeString(name);
        if (description != nullptr) SysFreeString(description);
        if (ports != nullptr) SysFreeString(ports);

        if (configured)
        {
            status = rules->Add(rule);
        }
        else
        {
            status = E_FAIL;
        }
        rule->Release();
        rules->Release();
        if (FAILED(status))
        {
            error = FormatHresult(status);
            return false;
        }
        return true;
    }

    bool RequestElevatedFirewallRepair(
        const unsigned short port,
        std::wstring& error)
    {
        error.clear();
        wchar_t executable[32'768] = {};
        const DWORD length = GetModuleFileNameW(
            nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (length == 0 || length >= std::size(executable))
        {
            error = L"The launcher path could not be resolved.";
            return false;
        }

        const std::wstring parameters = L"--repair-firewall --port " +
            std::to_wstring(port);
        SHELLEXECUTEINFOW request = {};
        request.cbSize = sizeof(request);
        request.fMask = SEE_MASK_NOCLOSEPROCESS;
        request.lpVerb = L"runas";
        request.lpFile = executable;
        request.lpParameters = parameters.c_str();
        request.nShow = SW_HIDE;
        if (!ShellExecuteExW(&request))
        {
            const DWORD code = GetLastError();
            error = code == ERROR_CANCELLED
                ? L"The firewall change was cancelled."
                : L"The firewall helper could not be started.";
            return false;
        }

        WaitForSingleObject(request.hProcess, 30'000);
        DWORD exitCode = 1;
        GetExitCodeProcess(request.hProcess, &exitCode);
        CloseHandle(request.hProcess);
        if (exitCode != 0)
        {
            error = L"Windows did not accept the firewall rule.";
            return false;
        }
        return true;
    }
}
