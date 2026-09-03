#pragma once

#include "../Automation/WindowControl.h"
#include "../Diagnostics/EventLog.h"
#include "MultiplayerTestContext.h"

#include <array>

namespace fable::launcher::multiplayer
{
struct Peer final
{
    std::wstring instance;
    std::wstring player;
    std::wstring role;
    std::wstring scenario;
    std::wstring fixtureSaveName;
    std::filesystem::path root;
    std::filesystem::path events;
    std::filesystem::path autoSave;
    std::filesystem::file_time_type initialAutoSaveWriteTime = {};
    std::uintmax_t initialAutoSaveSize = 0;
    std::array<std::uint8_t, 32> initialAutoSaveDigest = {};
    bool initialAutoSaveCaptured = false;
    runtime::LaunchedGame game;
};

class PeerHarness final
{
  public:
    explicit PeerHarness(const MultiplayerTestContext &context);

    bool PreparePeer(Peer &peer);
    bool SpawnPeer(Peer &peer, const wchar_t *address);
    bool WaitReady(Peer &peer, int x);
    bool WaitEvent(Peer &peer, const char *state);
    bool WaitEventDetail(Peer &peer, const char *state, const std::string &detail);
    bool WaitEventCount(Peer &peer, const char *state, std::size_t count);
    bool WaitEventCount(Peer &peer, const char *state, std::size_t count, unsigned int timeoutSeconds);
    bool WaitEventDetailCount(Peer &peer, const char *state, const std::string &detail, std::size_t count);
    bool WaitEventDetailCount(Peer &peer, const char *state, const std::string &detail, std::size_t count,
                              unsigned int timeoutSeconds);
    bool WaitBackgroundMovement(Peer &peer, std::uint64_t actorId);
    bool Focus(Peer &peer);
    bool Move(Peer &peer, unsigned int durationMilliseconds = 1'250, bool lateral = true);
    bool DriveFriendlyTargetedPvpAttacks(Peer &peer, unsigned int attacks);
    bool IsAlive(Peer &peer) const;
    bool IsResponsive(Peer &peer);
    bool Position(Peer &peer, int x, int y = 0);
    bool Stop(Peer &peer);
    bool PrepareRelaunch(Peer& peer, const wchar_t* scenario);
    bool WaitForAutoSaveWrite(Peer& peer);
    bool StopAll();

    const MultiplayerTestContext &context() const
    {
        return context_;
    }
    Peer &host()
    {
        return host_;
    }
    Peer &guest()
    {
        return guest_;
    }
    Peer &guest2()
    {
        return guest2_;
    }
    Peer &showcaseGuest(std::size_t index)
    {
        return showcaseGuests_[index];
    }

  private:
    Peer MakePeer(const wchar_t *instance, const wchar_t *player, const wchar_t *role, const wchar_t *scenario);
    const MultiplayerTestContext &context_;
    Peer host_;
    Peer guest_;
    Peer guest2_;
    std::array<Peer, 3> showcaseGuests_;
};
} // namespace fable::launcher::multiplayer
