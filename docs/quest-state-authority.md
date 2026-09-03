# Host-authoritative quest state

`QuestStateAuthorityService` owns one bounded opaque `CQuestManager` snapshot.
The host may publish a current snapshot through the reliable control stream;
the transfer is begin/chunk/commit, capped at 1 MiB, hashed, and fenced by
authority epoch, session revision, snapshot revision, transfer ID, source
actor, and transport connection nonce. The host retains the latest snapshot
and republishes it when `PeerSetRevision` changes, so a late guest does not
depend on a one-shot world-ready event.

The guest stores a complete validated snapshot and applies it at the
pre-world construction gate through the focused native quest bridge. Because
player-state and quest control use independent reliable streams, a sane Begin
may latch the first nonzero source actor before player identity arrives; later
player-state identity must confirm that latch. A mismatch invalidates the
provisional transfer and requires a fresh Begin from the confirmed actor. No
guest Hero-owned state is accepted by this service.

The bridge validates the current PE before installing the x86
`CQuestManager::SaveGameState` observer. It captures once when the host world
becomes ready and refreshes on later native saves. Guest apply follows the
validated retail sequence: CCharString source and empty context, 0x1C-byte
CStringParser with callback 0/mode 0, manager `LoadGameState`, then parser and
string destruction under guarded native cleanup.
