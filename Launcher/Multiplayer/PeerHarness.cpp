#include "PeerHarness.h"

#include "../Automation/InputStimulus.h"
#include "../Configuration/LauncherConstants.h"
#include "../Diagnostics/GameCompatibility.h"
#include "../Platform/Win32Error.h"

#include <Windows.h>
#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace fable::launcher::multiplayer
{
namespace
{
enum class EventExpectationKind
{
    State,
    Detail,
    Count,
    DetailCount,
    BackgroundMovement
};

struct EventExpectation final
{
    EventExpectationKind kind = EventExpectationKind::State;
    const char *state = nullptr;
    std::string detail;
    std::size_t count = 0;
    std::uint64_t actorId = 0;
};

bool MatchesExpectation(const std::string &events, const EventExpectation &expectation)
{
    switch (expectation.kind)
    {
    case EventExpectationKind::State:
        return diagnostics::EventWasReported(events, expectation.state);
    case EventExpectationKind::Detail:
        return diagnostics::EventDetailContains(events, expectation.state, expectation.detail.c_str());
    case EventExpectationKind::Count:
        return diagnostics::EventCount(events, expectation.state) >= expectation.count;
    case EventExpectationKind::DetailCount:
        return diagnostics::EventDetailCount(events, expectation.state, expectation.detail.c_str()) >=
               expectation.count;
    case EventExpectationKind::BackgroundMovement:
        return diagnostics::ReplicatedMovementWasApplied(events, expectation.actorId);
    }
    return false;
}

bool WaitForExpectation(Peer &peer, const EventExpectation &expectation, unsigned int timeoutSeconds)
{
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(timeoutSeconds) * 1'000;
    constexpr DWORD pollMilliseconds = 250;
    while (GetTickCount64() < deadline)
    {
        const std::string events = diagnostics::ReadEventFile(peer.events);
        if (diagnostics::EventWasReported(events, "ClientFailed"))
        {
            std::wcerr << L"Multiplayer " << peer.instance << L" reported ClientFailed.\n";
            return false;
        }
        if (MatchesExpectation(events, expectation))
        {
            return true;
        }

        const DWORD processState = WaitForSingleObject(peer.game.process.get(), pollMilliseconds);
        if (processState == WAIT_OBJECT_0)
        {
            DWORD exitCode = 0;
            if (GetExitCodeProcess(peer.game.process.get(), &exitCode))
            {
                std::wcerr << L"Multiplayer " << peer.instance
                           << L" exited before the expected event; exit code " << exitCode << L".\n";
            }
            else
            {
                const DWORD error = GetLastError();
                std::wcerr << L"Multiplayer " << peer.instance
                           << L" exited, and its exit code could not be read (" << error << L"): "
                           << platform::FormatWindowsError(error) << L".\n";
            }
            return false;
        }
        if (processState == WAIT_FAILED)
        {
            const DWORD error = GetLastError();
            std::wcerr << L"Multiplayer " << peer.instance
                       << L" process polling failed (" << error << L"): "
                       << platform::FormatWindowsError(error) << L".\n";
            return false;
        }
    }
    std::wcerr << L"Timed out waiting for the expected event from multiplayer " << peer.instance << L".\n";
    return false;
}

bool ActivateWindow(HWND window)
{
    MSG message = {};
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);

    const DWORD currentThread = GetCurrentThreadId();
    const DWORD targetThread = GetWindowThreadProcessId(window, nullptr);
    const HWND previousForeground = GetForegroundWindow();
    const DWORD foregroundThread = previousForeground != nullptr
        ? GetWindowThreadProcessId(previousForeground, nullptr)
        : 0;
    const bool attachedForeground = foregroundThread != 0 &&
        foregroundThread != currentThread &&
        AttachThreadInput(currentThread, foregroundThread, TRUE) != FALSE;
    const bool attachedTarget = targetThread != 0 &&
        targetThread != currentThread &&
        targetThread != foregroundThread &&
        AttachThreadInput(currentThread, targetThread, TRUE) != FALSE;

    ShowWindowAsync(window, SW_RESTORE);
    BringWindowToTop(window);
    SetForegroundWindow(window);
    SetFocus(window);

    if (attachedTarget)
    {
        AttachThreadInput(currentThread, targetThread, FALSE);
    }
    if (attachedForeground)
    {
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    }
    if (GetForegroundWindow() != window)
    {
        INPUT alt[2] = {};
        alt[0].type = INPUT_KEYBOARD;
        alt[0].ki.wVk = VK_MENU;
        alt[1] = alt[0];
        alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, alt, sizeof(INPUT));
        SetForegroundWindow(window);
    }

    const ULONGLONG deadline = GetTickCount64() + 1'000;
    while (GetForegroundWindow() != window && GetTickCount64() < deadline)
    {
        Sleep(25);
    }
    return GetForegroundWindow() == window;
}

bool ResolveFixtureAutoSave(Peer &peer)
{
    if (std::filesystem::is_regular_file(peer.autoSave))
    {
        return true;
    }

    const std::filesystem::path saveRoot = peer.root / L"Documents" /
        L"My Games" / L"FableHD" / L"Saves";
    std::error_code error;
    std::filesystem::path resolved;
    for (std::filesystem::directory_iterator iterator(saveRoot, error), end;
         !error && iterator != end;
         iterator.increment(error))
    {
        if (!iterator->is_directory(error))
        {
            continue;
        }
        const std::filesystem::path candidate =
            iterator->path() / L"AutoSave";
        const std::filesystem::path profile =
            iterator->path() / L"Profile.bin";
        if (!std::filesystem::is_regular_file(candidate, error) ||
            !std::filesystem::is_regular_file(profile, error))
        {
            continue;
        }
        if (!resolved.empty())
        {
            return false;
        }
        resolved = candidate;
    }
    if (error || resolved.empty())
    {
        return false;
    }
    peer.autoSave = std::move(resolved);
    return true;
}

bool ResolveFixtureSave(
    const Peer& peer,
    std::filesystem::path& resolved)
{
    resolved.clear();
    const std::filesystem::path saveRoot = peer.root / L"Documents" /
        L"My Games" / L"FableHD" / L"Saves";
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(saveRoot, error), end;
         !error && iterator != end;
         iterator.increment(error))
    {
        if (!iterator->is_directory(error))
        {
            continue;
        }
        const std::filesystem::path candidate =
            iterator->path() / peer.fixtureSaveName;
        if (!std::filesystem::is_regular_file(candidate, error))
        {
            continue;
        }
        if (!resolved.empty())
        {
            return false;
        }
        resolved = candidate;
    }
    return !error && !resolved.empty();
}

std::wstring DigestText(const std::array<std::uint8_t, 32>& digest)
{
    constexpr wchar_t digits[] = L"0123456789ABCDEF";
    std::wstring result;
    result.reserve(digest.size() * 2);
    for (const std::uint8_t byte : digest)
    {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0F]);
    }
    return result;
}

std::filesystem::path ResolvePeerFixtureDocuments(
    const MultiplayerTestContext& context,
    const Peer& peer)
{
    const wchar_t* const side = peer.role == L"host" ? L"host" : L"guest";
    const std::filesystem::path paired =
        context.fixtureDocumentsSource / side / L"Documents";
    std::error_code error;
    if (std::filesystem::is_directory(paired, error) && !error)
    {
        return paired;
    }
    return context.fixtureDocumentsSource;
}

const wchar_t *ScenarioName(const MultiplayerTestContext &context, bool host)
{
    const wchar_t *prefix = host ? L"multiplayer_host" : L"multiplayer_guest";
    switch (context.scenario)
    {
    case MultiplayerScenario::HeroWill:
        return host ? L"multiplayer_host_hero_will" : L"multiplayer_guest_hero_will";
    case MultiplayerScenario::Combat:
    case MultiplayerScenario::ManualCombat:
        return host ? L"multiplayer_host_combat" : L"multiplayer_guest_combat";
    case MultiplayerScenario::Authority:
        return host ? L"multiplayer_host_authority" : L"multiplayer_guest_authority";
    case MultiplayerScenario::Transition:
        return host ? L"multiplayer_host_transition" : L"multiplayer_guest_transition";
    case MultiplayerScenario::MapStress:
        return host ? L"multiplayer_host_map_stress" : L"multiplayer_guest_map_stress";
    case MultiplayerScenario::Save:
        return host ? L"multiplayer_host_save" : L"multiplayer_guest_save";
    default:
        return prefix;
    }
}
} // namespace

PeerHarness::PeerHarness(const MultiplayerTestContext &context)
    : context_(context), host_(MakePeer(L"host", L"Host", L"host", ScenarioName(context, true))),
      guest_(MakePeer(L"guest", L"Guest", L"guest", ScenarioName(context, false))),
      guest2_(MakePeer(L"guest2", L"Guest Two", L"guest", ScenarioName(context, false)))
{
    const wchar_t *scenario = ScenarioName(context, false);
    showcaseGuests_[0] = MakePeer(L"guest3", L"Guest Three", L"guest", scenario);
    showcaseGuests_[1] = MakePeer(L"guest4", L"Guest Four", L"guest", scenario);
    showcaseGuests_[2] = MakePeer(L"guest5", L"Guest Five", L"guest", scenario);
}

Peer PeerHarness::MakePeer(const wchar_t *instance, const wchar_t *player, const wchar_t *role, const wchar_t *scenario)
{
    Peer peer;
    peer.instance = instance;
    peer.player = player;
    peer.role = role;
    peer.scenario = scenario;
    peer.fixtureSaveName = peer.role == L"host"
        ? context_.hostFixtureSave
        : context_.guestFixtureSave;
    peer.root = context_.sessionRoot / instance;
    peer.events = peer.root / L"events.jsonl";
    peer.autoSave = peer.root / L"Documents" / L"My Games" /
        L"FableHD" / L"Saves" / L"Hero1" / L"AutoSave";
    return peer;
}

bool PeerHarness::PreparePeer(Peer &peer)
{
    std::error_code error;
    std::filesystem::create_directories(peer.root / L"Documents", error);
    if (!error)
    {
        std::filesystem::create_directories(peer.root / L"script-data", error);
    }
    if (!error)
    {
        const std::filesystem::path fixtureDocuments =
            ResolvePeerFixtureDocuments(context_, peer);
        std::filesystem::copy(
            fixtureDocuments, peer.root / L"Documents",
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, error);
    }
    if (!error)
    {
        std::filesystem::path selectedSave;
        std::array<std::uint8_t, 32> selectedDigest = {};
        if (!ResolveFixtureSave(peer, selectedSave) ||
            !diagnostics::Sha256File(selectedSave, selectedDigest))
        {
            std::wcerr << L"Could not resolve the exact fixture save for "
                       << peer.instance << L": " << peer.fixtureSaveName
                       << L".\n";
            return false;
        }
        std::wcout << L"Fixture: role=" << peer.instance
                   << L" save=\"" << peer.fixtureSaveName
                   << L"\" sha256=" << DigestText(selectedDigest)
                   << L" path=" << selectedSave.wstring() << L"\n";
        if (!ResolveFixtureAutoSave(peer))
        {
            return false;
        }
        peer.initialAutoSaveWriteTime =
            std::filesystem::last_write_time(peer.autoSave, error);
    }
    if (!error)
    {
        peer.initialAutoSaveSize =
            std::filesystem::file_size(peer.autoSave, error);
    }
    peer.initialAutoSaveCaptured = !error &&
        diagnostics::Sha256File(
            peer.autoSave,
            peer.initialAutoSaveDigest);
    return peer.initialAutoSaveCaptured;
}

bool PeerHarness::SpawnPeer(Peer &peer, const wchar_t *address)
{
    runtime::GameLaunchSpec spec;
    spec.executable = context_.executable;
    spec.clientDll = context_.clientDll;
    spec.clientLog = peer.root / L"client.log";
    spec.eventPath = peer.events;
    spec.fixtureDocuments = peer.root / L"Documents";
    spec.fixtureSaveName = peer.fixtureSaveName;
    spec.scriptData = peer.root / L"script-data";
    spec.clientMode = L"observe";
    spec.scenario = peer.scenario;
    spec.runId = context_.sessionId + L"-" + peer.instance;
    spec.localSession = context_.sessionId;
    spec.localInstance = peer.instance;
    spec.multiplayerRole = peer.role;
    spec.multiplayerAddress = address == nullptr ? L"" : address;
    spec.multiplayerPort = context_.port;
    spec.multiplayerPlayerId = peer.player;
    spec.multiplayerAppearance = kRemoteHeroDefinition;
    spec.mapStressSeed = context_.mapStressSeed;
    spec.mapStressTransitions = context_.mapStressTransitions;
    spec.arguments = context_.gameArguments;
    return runtime::SpawnGame(spec, peer.game);
}

bool PeerHarness::WaitReady(Peer &peer, int x)
{
    return automation::WaitForLocalInstanceReady(peer.game, peer.events, peer.instance.c_str(), x,
                                                 context_.timeoutSeconds);
}

bool PeerHarness::WaitEvent(Peer &peer, const char *state)
{
    return WaitForExpectation(peer, EventExpectation{EventExpectationKind::State, state}, context_.timeoutSeconds);
}

bool PeerHarness::WaitEventDetail(Peer &peer, const char *state, const std::string &detail)
{
    return WaitForExpectation(peer, EventExpectation{EventExpectationKind::Detail, state, detail},
                              context_.timeoutSeconds);
}

bool PeerHarness::WaitEventCount(Peer &peer, const char *state, std::size_t count)
{
    return WaitEventCount(peer, state, count, context_.timeoutSeconds);
}

bool PeerHarness::WaitEventCount(Peer &peer, const char *state, std::size_t count, unsigned int timeoutSeconds)
{
    return WaitForExpectation(peer, EventExpectation{EventExpectationKind::Count, state, {}, count}, timeoutSeconds);
}

bool PeerHarness::WaitEventDetailCount(Peer &peer, const char *state, const std::string &detail, std::size_t count)
{
    return WaitEventDetailCount(peer, state, detail, count, context_.timeoutSeconds);
}

bool PeerHarness::WaitEventDetailCount(Peer &peer, const char *state, const std::string &detail, std::size_t count,
                                       unsigned int timeoutSeconds)
{
    return WaitForExpectation(peer, EventExpectation{EventExpectationKind::DetailCount, state, detail, count},
                              timeoutSeconds);
}

bool PeerHarness::DriveFriendlyTargetedPvpAttacks(Peer &peer, unsigned int attacks)
{
    automation::ScopedSyntheticKey target(VK_SPACE);
    if (attacks == 0)
    {
        return false;
    }
    for (unsigned int ordinal = 1; ordinal <= attacks; ++ordinal)
    {
        const std::string requested = "ordinal=" + std::to_string(ordinal) + " action=SPACE";
        if (!WaitEventDetail(peer, "MultiplayerCombatPvpTargetInputRequested", requested) ||
            !Focus(peer))
        {
            return false;
        }
        if (!target.Press())
        {
            std::wcerr << L"Could not press the friendly-target action in multiplayer "
                       << peer.instance << L".\n";
            return false;
        }
        const bool observed = WaitEventDetail(peer, "MultiplayerCombatPvpFriendlyTargetObserved", requested);
        const bool released = target.Release();
        if (!observed || !released)
        {
            if (!released)
            {
                std::wcerr << L"Could not release the friendly-target action in multiplayer "
                           << peer.instance << L".\n";
            }
            return false;
        }
        Sleep(250);
    }
    return true;
}

bool PeerHarness::IsAlive(Peer &peer) const
{
    return WaitForSingleObject(peer.game.process.get(), 0) == WAIT_TIMEOUT;
}

bool PeerHarness::IsResponsive(Peer &peer)
{
    peer.game.window = automation::FindMainWindow(peer.game.processId);
    return peer.game.window != nullptr && automation::WindowIsResponsive(peer.game.window);
}

bool PeerHarness::WaitBackgroundMovement(Peer &peer, std::uint64_t actorId)
{
    return WaitForExpectation(peer,
                              EventExpectation{EventExpectationKind::BackgroundMovement, nullptr, {}, 0, actorId},
                              context_.timeoutSeconds);
}

bool PeerHarness::Focus(Peer &peer)
{
    peer.game.window = automation::FindMainWindow(peer.game.processId);
    if (peer.game.window == nullptr || !IsWindow(peer.game.window))
    {
        std::wcerr << L"Multiplayer " << peer.instance
                   << L" has no game window to activate.\n";
        return false;
    }
    if (!ActivateWindow(peer.game.window))
    {
        std::wcerr << L"Windows refused to activate multiplayer " << peer.instance << L".\n";
        return false;
    }
    Sleep(500);
    return true;
}

bool PeerHarness::Move(Peer &peer, unsigned int durationMilliseconds, bool lateral)
{
    peer.game.window = automation::FindMainWindow(peer.game.processId);
    if (peer.game.window == nullptr || !IsWindow(peer.game.window))
    {
        std::wcerr << L"Multiplayer " << peer.instance
                   << L" has no game window for movement.\n";
        return false;
    }
    if (!ActivateWindow(peer.game.window))
    {
        std::wcerr << L"Could not activate multiplayer " << peer.instance << L".\n";
        return false;
    }
    Sleep(250);
    automation::ScopedSyntheticKey forward('W');
    automation::ScopedSyntheticKey side('D');
    if (!forward.Press() || (lateral && !side.Press()))
    {
        forward.Release();
        side.Release();
        std::wcerr << L"Could not press movement input in multiplayer "
                   << peer.instance << L".\n";
        return false;
    }
    std::wcout << L"Multiplayer test: moving " << peer.instance
               << L" through Fable's normal player input.\n";
    Sleep(durationMilliseconds);
    const bool forwardReleased = forward.Release();
    const bool sideReleased = !lateral || side.Release();
    if (!forwardReleased || !sideReleased)
    {
        std::wcerr << L"Could not release movement input in multiplayer "
                   << peer.instance << L".\n";
        return false;
    }
    return true;
}

bool PeerHarness::Position(Peer &peer, int x, int y)
{
    return automation::RepositionLocalInstanceWindow(peer.game, peer.instance.c_str(), x, y);
}

bool PeerHarness::Stop(Peer &peer)
{
    if (!peer.game.process.valid())
        return true;
    const bool stopped =
        runtime::CloseCreatedProcess(peer.game.process.get(), peer.game.processId, peer.game.shutdownEvent.get());
    if (stopped)
    {
        peer.game.process.reset();
        peer.game.shutdownEvent.reset();
        peer.game.processId = 0;
        peer.game.window = nullptr;
    }
    return stopped;
}

bool PeerHarness::PrepareRelaunch(
    Peer& peer,
    const wchar_t* const scenario)
{
    if (peer.game.process.valid() || scenario == nullptr ||
        scenario[0] == L'\0')
    {
        return false;
    }
    std::error_code error;
    std::filesystem::remove(peer.events, error);
    if (error)
    {
        return false;
    }
    std::filesystem::remove(peer.root / L"client.log", error);
    if (error)
    {
        return false;
    }
    peer.scenario = scenario;
    return true;
}

bool PeerHarness::WaitForAutoSaveWrite(Peer& peer)
{
    if (!peer.initialAutoSaveCaptured)
    {
        return false;
    }

    const ULONGLONG deadline = GetTickCount64() +
        static_cast<ULONGLONG>(context_.timeoutSeconds) * 1'000;
    std::filesystem::file_time_type lastWrite = {};
    std::uintmax_t lastSize = 0;
    std::array<std::uint8_t, 32> lastDigest = {};
    ULONGLONG stableSince = 0;
    while (GetTickCount64() < deadline)
    {
        if (!IsAlive(peer))
        {
            std::wcerr << L"Multiplayer " << peer.instance
                       << L" exited while its native save was being written.\n";
            return false;
        }

        std::error_code error;
        const auto writeTime =
            std::filesystem::last_write_time(peer.autoSave, error);
        const std::uintmax_t size = !error
            ? std::filesystem::file_size(peer.autoSave, error)
            : 0;
        std::array<std::uint8_t, 32> digest = {};
        const bool digestRead = !error && size != 0 &&
            diagnostics::Sha256File(peer.autoSave, digest);
        if (digestRead && digest != peer.initialAutoSaveDigest)
        {
            const ULONGLONG now = GetTickCount64();
            if (writeTime != lastWrite || size != lastSize ||
                digest != lastDigest)
            {
                lastWrite = writeTime;
                lastSize = size;
                lastDigest = digest;
                stableSince = now;
            }
            else if (stableSince != 0 && now - stableSince >= 2'000)
            {
                return size != 0;
            }
        }
        Sleep(250);
    }

    std::wcerr << L"Timed out waiting for multiplayer " << peer.instance
               << L" to finish writing AutoSave.\n";
    return false;
}

bool PeerHarness::StopAll()
{
    bool stopped = true;
    for (Peer &peer : showcaseGuests_)
        stopped = Stop(peer) && stopped;
    stopped = Stop(guest2_) && stopped;
    stopped = Stop(guest_) && stopped;
    stopped = Stop(host_) && stopped;
    return stopped;
}
} // namespace fable::launcher::multiplayer
