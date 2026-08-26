#pragma once

#include "Multiplayer/Runtime/MultiplayerRuntimeGraph.h"

namespace fable::multiplayer
{
    // Compatibility name retained for launcher and automation callers. The
    // runtime graph is the single owner of the multiplayer lifecycle; keeping
    // a second forwarding façade only duplicated its public surface and
    // shutdown ordering.
    using MultiplayerSession = MultiplayerRuntimeGraph;
}
