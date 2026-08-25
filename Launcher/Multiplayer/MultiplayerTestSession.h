#pragma once

#include "PeerHarness.h"
#include "../Platform/ScopedEnvironmentVariable.h"

namespace fable::launcher::multiplayer
{
class MultiplayerTestSession final
{
public:
    explicit MultiplayerTestSession(const MultiplayerTestContext& context);
    ~MultiplayerTestSession();

    bool Prepare();
    bool StartCorePeers();
    bool StartRosterPeers();
    bool PositionWindows();
    void LeaveRunning();
    bool Shutdown();
    PeerHarness& peers() { return peers_; }
    const MultiplayerTestContext& context() const { return context_; }

private:
    const MultiplayerTestContext& context_;
    PeerHarness peers_;
    bool leaveRunning_ = false;
    fable::launcher::platform::ScopedEnvironmentVariable manualPlaytestEnvironment_;
};
}
