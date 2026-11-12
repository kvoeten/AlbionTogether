# Host-authoritative quest state

`QuestStateAuthorityService` owns one bounded opaque `CQuestManager` snapshot.
The host may publish a current snapshot through the reliable control stream;
the transfer is begin/chunk/commit, capped at 1 MiB, hashed, and fenced by
authority epoch, session revision, snapshot revision, transfer ID, source
actor, and transport connection nonce. The host retains the latest snapshot
and captures a fresh native snapshot when `PeerSetRevision` changes, so a late
or replacement guest never receives a stale cached campaign baseline.

The guest stores a complete validated snapshot and applies it at the
pre-world construction gate through the focused native quest bridge. Because
player-state and quest control use independent reliable streams, a sane Begin
may latch the first nonzero source actor before player identity arrives; later
player-state identity must confirm that latch. A mismatch invalidates the
provisional transfer and requires a fresh Begin from the confirmed actor. No
guest Hero-owned state is accepted by this service.

The snapshot also carries a distinct `campaignRevision`. Snapshot revision
identifies a transfer; campaign revision orders semantic quest changes and is
never inferred from the transfer number.

After the snapshot has applied, `QuestLifecycleHook` observes only genuine
native Activate/Deactivate/Complete/Fail state edges. These are copied into a
bounded queue and sent as reliable ordered Proposal/Commit messages. The host
assigns the next campaign revision, applies a guest proposal through the
original native quest entry point, and broadcasts one idempotent commit. The
originating guest treats that commit as confirmation and does not replay its
already-applied transition. Commits which arrive before the guest finishes
applying its snapshot are buffered, then source/session/revision checked; they
are never applied ahead of the baseline.

This is event-driven. There is no per-frame quest-state scan. Native quest
machines continue locally, while the snapshot establishes current state and
the semantic lane carries only subsequent changes. A rejected or conflicting
proposal asks the host to capture a fresh repair snapshot.
If the bounded native-event queue ever overflows, delivery pauses instead of
silently skipping an already-applied transition; the host captures a repair
snapshot directly, or a guest sends an explicit repair request first.

Quest lifecycle authority and world-command authority are intentionally
separate. Any player currently in the authoritative map incarnation may
advance a shared quest. Consequential script commands such as spawning or
retiring shared actors must additionally require that map's simulation owner;
that command gate is not implied by accepting the quest edge itself.

`QuestPolicyRegistry` classifies all 200 installed `AddQuest` registrations.
Shared story edges enter the ordered campaign lane; private tutorials and
Hero-only minigames stay local; true global managers remain host-owned; debug
and unknown names fail closed. The same rows record map-owner, initiator-choice,
shared-transition, and reward intent so the native command layers can enforce
those concerns without baking quest-name branches into networking code. Every
observed edge logs its resolved policy, making front-to-back playthrough notes
actionable without adding a polling observer.

The bridge validates the current PE before installing the x86
`CQuestManager::SaveGameState` observer. It captures once when the host world
becomes ready and refreshes on later native saves. Guest apply follows the
validated retail sequence: CCharString source and empty context, 0x1C-byte
CStringParser with callback 0/mode 0, manager `LoadGameState`, then parser and
string destruction under guarded native cleanup.

Outcome-changing conversations need a separate shared-decision lease. The
initiator will be the only peer allowed to answer while all participating
peers remain at a read-only barrier; the host will commit the chosen branch
and release the barrier. No choice-answer hook is enabled until its current
native entry point has been validated.
