# Host-authoritative quest state

`QuestStateAuthorityService` owns one bounded opaque `CQuestManager` snapshot.
The host may publish a current snapshot through the reliable control stream;
the transfer is begin/chunk/commit, capped at 1 MiB, hashed, and fenced by
authority epoch, session revision, snapshot revision, transfer ID, source
actor, and transport connection nonce. The host retains the latest snapshot
and republishes it when `PeerSetRevision` changes, so a late guest does not
depend on a one-shot world-ready event.

## Continuous progression capture

While the host Hero owns a stable live world, the presentation lifecycle
samples the validated native `CQuestManager::SaveGameState` seam at a bounded
one-second interval. Serialized output is compared against the retained host
snapshot before a new revision is created. Identical output is discarded
byte-for-byte, so ordinary polling produces no additional reliable quest
traffic when the quest manager has not changed.

A changed snapshot advances exactly one host snapshot revision and enters the
existing reliable begin/chunk/commit path. This preserves the same authority,
session, source-actor, nonce, size, and hash fences used by initial world-load
state and late-join publication.

## Guest staging and live apply

The guest stores only a complete validated host snapshot. Hero-owned guest
state never enters this service.

Initial world construction still uses the exact validated native QUESTS load
override. The host parser replaces the guest parser at the retail
`CQuestManager::LoadGameState` boundary before REGIONS and FACTIONS continue.
That remains the authoritative fallback path for every staged snapshot.

For progression received while a guest is already playing, the runtime may
attempt a controlled live apply only after all of the following are true:

- the reliable transfer completed and passed its hash and fencing checks;
- the local Hero owns a fully loaded, current world;
- no source-map teardown or destination transition path is active; and
- the exact validated QUESTS load override is installed.

The live apply uses the same retail `CStringParser` construction sequence and
`CQuestManager::LoadGameState` function that the validated QUESTS boundary
uses. Each received snapshot revision/fingerprint pair is attempted at most
once by the presentation lifecycle. If the native live apply is unavailable or
fails, the snapshot remains staged and is not discarded; the next validated
world-load QUESTS boundary can still apply it.

## Native boundary

The bridge validates the current PE before installing the x86
`CQuestManager::SaveGameState` observer. It captures once when the host world
becomes ready, refreshes on later native saves, and is also sampled during
stable active play. Guest apply follows the validated retail sequence:
CCharString source and empty context, 0x1C-byte CStringParser with callback
0/mode 0, manager `LoadGameState`, then parser and string destruction under
guarded native cleanup.

Player-state and quest control use independent reliable streams. A sane Begin
may latch the first nonzero source actor before player identity arrives; later
player-state identity must confirm that latch. A mismatch invalidates the
provisional transfer and requires a fresh Begin from the confirmed actor.
