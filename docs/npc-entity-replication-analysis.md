# NPC entity replication analysis

## Verdict

The proposed ownership model fits Fable's native creature stack, with one
important refinement: map simulation authority and action authority must be
separate leases.

- The host owns persistent NPC truth: identity, current map, availability,
  schedule state, inventory, health, and final conflict resolution.
- One peer receives a fenced simulation lease for each populated map.
- An entity can temporarily receive a narrower action lease, such as dialogue
  ownership or combat ownership, without transferring the host's persistent
  record.
- Native actions are replicated semantically at their begin/update/end
  boundaries. Movement snapshots correct drift while a movement-bearing action
  is active.

This is preferable to mirroring every native field or polling all NPCs. It
preserves the retail AI, navigation, animation, dialogue, and combat stacks,
while replication remains bounded by entity and action lifetimes.

## Native evidence

### Stable identity

`CScriptThing` vtable slot 12 delegates to `CGameScriptThing` slot 12 and
returns a 64-bit value in `EDX:EAX`. The implementation stores that value at
`+0x18/+0x1C`. A script handle can therefore expose the engine's existing
64-bit Thing UID without using a process-local pointer as network identity.

The save-state loader also reads these sections:

- `GlobalUIDCount`
- `SAVED_ENTITIES`
- `SAVED_NPC_NAMES`

This strongly supports a host-side persistent NPC directory keyed by native
Thing UID. The UID still needs a runtime stability test across map unload,
reload, and save reload before it becomes protocol-stable. A generation and
lease epoch must fence recycled native instances regardless.

### Current map and persistence

`CThing_LoadFromLevelScript` reads `ThingCurrentMapID` into `CThing +0x9A`.
When the serialized value is not used, it initializes the same field from the
current map manager. The same loader reads `ThingGamePersistent` and
`ThingLevelPersistent` into flag bits at `CThing +0x9E`: game persistence is
bit `0x20`, and level persistence is bit `0x10`.

The audited writers of `CThing +0x9A` are construction, level-script load, and
native reinitialization paths. There is no evidence of a safe general-purpose
live `SetMap` operation. Cross-map travel is therefore represented as one
fenced lifecycle transaction: the source owner unregisters the Thing with its
final transform and destination map ID, the host commits the destination while
keeping the same generation, and the destination owner acknowledges the new
native incarnation. Late source teardown cannot pull the record back because
it carries the old source map and lease epoch.

`CTCMapwho` (component `0x3A`) registers a live Thing with the current world's
spatial structures. Position changes rebucket it and destruction unregisters
it. The serialized map ID is persistent placement; `CTCMapwho` is the live
map-presence boundary.

Destination materialization uses the retail creature factory and the saved
definition index. `CThing +0x98` resolves through
`CThingManager -> CDefinitionManager` (`0x016CB160` and `0x016CABB0` in the
preferred image) to recover the definition name. A materialized replica keeps
its process-local native UID; a bounded one-to-one alias table maps that UID to
the host's canonical UID. The implementation never patches Fable's native UID
registry.

The host can therefore resolve cross-map conflicts against an engine concept
that already exists. It does not need an unbounded parallel position history.

### Universal creature-action boundary

`CThingCreature_SubmitAction` at preferred address `0x01F42F70` is the central
action submission/replacement method:

- `this` is the `CThingCreature`;
- the sole argument is a `CCreatureActionBase`;
- it checks action priority;
- it clones or retains the accepted action;
- it installs the active action at creature `+0x120`, with counted-pointer
  metadata at `+0x124`;
- it attaches/starts the accepted action;
- it returns whether submission succeeded.

`CThingCreature_UpdateActiveAction` at `0x01F42E20` updates the active action
and promotes queued work from `+0x128`. The common action update wrapper is at
`0x01BEB710`. Shared vtable slot 4 reaches `CCreatureAction_Finish` at
`0x01BEF370`, which marks the action finished and performs its end/follow-up
cleanup. Concrete RTTI covers movement, combat, speech, conversation
animation, shops, objects, spells, and ambient behaviors.

This is the correct broad action observation seam. It is much more complete
than hooking individual AI decisions or input devices.

### AI, schedules, and random population

The established live stack remains:

1. `CAIBrain` selects autonomous intent.
2. `CTCScriptedControl` submits explicit scripted actions.
3. Creature navigation resolves the active movement-bearing action.
4. Physics applies displacement.
5. Creature frame update derives native motion values.
6. Creature-mode and animation components select and play locomotion.

Ownership now fences the decision and action layers as well as movement.
`CAIBrain +0x20` is the exact owning `CThingAICreature` supplied to the brain
constructor. The validated brain-update hook resolves that Thing and runs the
fiber only when this peer is the entity publisher. The same policy gates both
`CThingCreature_UpdateActiveAction` (`0x01F42E20`) and
`CThingCreature_SubmitAction` (`0x01F42F70`). A non-owner therefore retains the
native body, physics, animation, and future replay surface without independently
choosing targets, advancing a stale attack, or originating another action. A
main-attacker action lease moves all three publisher decisions together.

The native hooks do not traverse the mutable authority or lifecycle maps.
Reconciliation publishes a bounded immutable decision table keyed by the exact
native creature incarnation, and the hot AI/action hooks atomically consume that
table. This keeps a possible worker-thread brain/action call from racing the
game-thread lease and Mapwho updates. Unknown bootstrap Things remain fail-open;
replicable non-Hero creatures receive an explicit decision as soon as their
presence event is reconciled.

`CTCIdleScheduler` (component `0xD3`) is reset when a creature brain is created
and owns action/schedule-like containers. It is live-AI state, not the durable
off-map villager record.

The durable low-simulation seam is `CTCDummyVillager` (component `0xD6`). Its
retail serializer at `0x01D84600` persists `HomeBuildingUID` at component
`+0x18`, `WorkBuildingUID` at `+0x28`, `CreatureUID` at `+0x38`, the next
recreation day/frame at `+0x40/+0x44`, and `Respawnable`/`Guard` flags at
`+0x48/+0x49`. Its update path uses the shared world clock, village component,
building markers, and the saved creature UID to recreate a live villager. This
is the bounded behind-the-scenes NPC row we need; it confirms that off-map
authority does not require accumulating sampled positions.

The shipped editable TNG data gives a cleaner persistence view. A village is a
persistent `Village` Thing with `CTCVillage::HasBeenInitiallyPopulated`.
Persistent villagers carry a `CTCVillageMember::VillageUID`, their native
position, and in specialized cases `HomeBuildingUID` or `WorkBuildingUID`.
Idle definitions include home, pub, shopkeeper, and village-task state groups.
Ordinary daily behavior is reconstructed by the retail AI from shared time,
village membership, the `CTCDummyVillager` building assignments/recreation
deadline, and current Thing state when a map is live; it is not a second
continuously growing off-map movement history we need to mirror.

Random population is a separate system (`CV_RandomPopulationSimScript`,
`ProcessAlbionPopulationSim` at `0x01A08E80`, and
`HighDetailPopulationSim` at `0x01A09DD0`). The first pass advances Albion's
global/low-detail population state. The second consumes
`RandomPopulationSpawn`, `RandomPopulationExit`, villager, guard, and bandit
definitions for the currently loaded high-detail map.

The targeted native pass now bounds that low-detail state precisely.
`sub_1A07C40` maps the supported Greatwood, Darkwood, Witchwood, and Lookout
Point maps to region index `CV_RandomPopulationSimScript +0x70`.
`ProcessAlbionPopulationSim` writes an active flag at `+0x6C` and three target
counts at `+0x8C/+0x90/+0x94`. `HighDetailPopulationSim` compares its local
live counts at `+0x74/+0x78/+0x7C` against those targets, then uses the local
`RandomPopulationSpawn`/`RandomPopulationExit` definitions to materialize or
remove villagers, guards, and bandits. This is a bounded four-region projection,
not an off-map actor-position table.

Both retail callbacks now have authority fences. Only the host runs
`ProcessAlbionPopulationSim`; only the peer holding the current map simulation
lease runs `HighDetailPopulationSim`. The hooks preserve the retail body on
the selected peer. The host replicates the current active flag, region factors,
and target counts for each observed region; a guest cannot enter the retail
high-detail pass until that host projection for its region is present. Native
spawn/despawn behavior then flows through the ordinary Thing lifecycle observer
and becomes canonical host state. A deterministic host population seed and
semantic spawn decision message remain follow-up work; independent `rand()`
decisions on multiple peers are no longer allowed. A guest's first owner
snapshot is no longer allowed to seed an already-known map from that guest's
Hero save: the host advances the existing canonical-roster boundary into the
new lease epoch, and the owner must reconcile/materialize it before
high-detail population, AI, actions, movement, or lifecycle publication unlock.
A genuinely unseen map remains explicitly distinguishable and currently uses a
host-issued, owner-and-epoch-fenced first-owner seed permission. No guest may
interpret a missing baseline as permission to seed. The saved-map baseline
service now captures the host's complete bounded native map collection and
installs the exact record on a guest before its grant. That binary record
already includes `CTCDummyVillager` rows, so home/work assignments, recreation
deadlines, respawn/guard state, and the host dormant roster precede high-sim
ownership without a parallel custom schedule table.

The binary and shipped TNG data do not currently support the stronger theory
that every villager has a continuously advanced off-map locomotion row. The
evidence instead points to persistent village/building membership plus shared
time and live AI reconstruction. Explicit NPC travel between maps is still a
real durable mutation, but it belongs to the UID-keyed lifecycle record rather
than a sampled schedule history. The host remains the sole owner of the retail
Albion low-detail pass and resolves empty-map records to dormant state.

## Replication model

### Host save and player saves

The host's world save is the durable authority for shared world state. NPC
presence and schedules, quests, doors, stores, property ownership, shared
cutscene outcomes, and other world flags are accepted by the host and written
through the host's normal live/save state. A guest that owns a map publishes
only the mutations caused while it held the fenced map or action lease. The
host validates those mutations, applies them to its canonical world, and
replicates the accepted result.

Raw save files should not be streamed continuously. Network messages carry
typed mutations and bounded current baselines; the host save is their durable
backing. This avoids racing whole-file snapshots and allows immediate action,
combat, dialogue, and map-transition replication.

Each player's selected Hero save remains authoritative for personal inventory,
stats, progression, appearance, and equipment. A feature may deliberately
promote a personal mutation into shared world state, such as spending an item
in a server-owned trade, but that requires an explicit validated transaction.

The native binary provides a useful projection boundary rather than one
indivisible save blob. `CGameState_LoadTngState` at preferred address
`0x01F53440` reads `GlobalUIDCount`, `SAVED_ENTITIES`, and
`SAVED_NPC_NAMES`. The adjacent `CGameState_SaveTngState` at preferred address
`0x01F53F00` writes those same TNG sections. This is a strong candidate for
capturing and applying the host's shared-world baseline while leaving the
guest's Hero-owned sections sourced from its selected local save.

The first native persistence seam is now installed at both
`CThing_SaveToLevelScript` (`0x01F2DD10`) and
`CThing_LoadFromLevelScript` (`0x01F30C20`). When the host serializes a live
persistent Thing, the hook temporarily projects the directory's canonical map
ID into the normal retail serializer and restores the runtime value afterward.
When the retail base loader reconstructs a Thing, the hook projects the
canonical map immediately after UID, definition index, script identity, and map
ID have been read, before the derived creature components and Mapwho presence
finish construction. This deliberately does not run for guests or Hero-owned
save data.

Native Thing UIDs may differ between save-derived incarnations. The load/save
projection first resolves the bounded local-to-canonical UID alias and then
falls back to an exact script-name plus definition-index identity only when that
identity is unique in the canonical directory. The alias is retained for the
incarnation, allowing ordinary lifecycle reconciliation to remove a stale
source-map copy after an NPC has crossed maps without patching Fable's UID
registry.

`CSavedEntities_Save` (`0x01F4F210`) serializes the separate dormant-entity
collection in `CGameState +0xB8`. Its binary and text loaders place opaque
level-script blobs in 0x1C-byte records. Save-side reconstruction in
`sub_1F51F80` compares each live Thing's `CThing +0x9A` map ID with the vector
index, proving that the record index is the native map ID. It is still a map
record rather than an NPC identity, so it must not be used as a network entity
key. Separate dead-Thing entries remain outside this vector.

The client now observes both `CSavedEntities_LoadBinary` (`0x01F52D90`) and
`CSavedEntities_LoadText` (`0x01F527F0`) after the retail loader returns. The
observer validates the vector and each populated record, then reports the
current byte count and content hash per load under strict map-count and
per-record bounds. It is deliberately read-only until real saves confirm the
layout and payload sizes. The next bridge will keep at most one replace-in-place
blob per map, not append save history, and will associate materialized Things
with canonical UIDs through the ordinary lifecycle boundary.

A direct mutation bridge for those opaque dormant level blobs is not yet safe:
accepted changes must be associated with Thing UIDs inside the serialized
level script. The existing Thing load seam reconciles a stale blob whenever
its Thing is next reconstructed during the same session.
Canonical off-map state still is not fully durable across a host restart when
the host never loaded that NPC's map, because the canonical directory itself is
not yet persisted independently.

The session therefore uses three ownership domains:

- **Shared world:** the host save owns NPC placement/availability, quests,
  doors, properties, stores, shared cutscene outcomes, and other world flags.
- **Local Hero:** each player save owns inventory, stats, progression,
  appearance, equipment, and other personal character state.
- **Validated transaction:** operations that touch both domains, such as a
  store purchase or property trade, commit only after the host accepts the
  world mutation and the initiating client applies the matching Hero mutation.

A guest never writes directly into the canonical host save. It submits a typed
mutation carrying the relevant map/action lease epoch and the world revision it
observed. The host validates and applies that mutation through the native game
setter, advances the canonical revision, and broadcasts the accepted result.
Conflicting or stale mutations lose to the host revision and are repaired from
a bounded current baseline.

This durable projection does not replace live replication. In-progress
movement, combat, dialogue, and animation remain ephemeral replicated actions;
only their durable outcomes are committed into host world state.

### Persistent entity directory

The host keeps one current record per entity, not a historical state store:

```text
EntityKey       native Thing UID + host-issued generation
Definition      creature/object archetype and persistent script identity
Presence        current map, position/yaw, available/dormant/dead, incarnation
Durable state   schedule cursor, health, store state, selected game flags
Lease fence     map lease epoch and action lease epoch
```

Live movement samples and completed actions expire. Despawn retires the live
incarnation while the compact persistent record remains only when the game
itself considers the entity persistent.

Each accepted movement sample replaces the entity's single checkpointed
position and yaw in the host directory; it does not append history or advance a
save revision every frame. The reliable lifecycle baseline includes that
checkpoint, while the lossy movement channel remains responsible for smooth
live presentation. Dormancy or a map crossing then commits the final current
checkpoint as part of the lifecycle mutation.

### Map simulation lease

The host grants one peer authority to simulate a map:

- each player reaching a native region boundary submits a `Prepare` to the
  host; this atomically reserves arrival order and starts the destination
  saved-map baseline without changing either map lease;
- after source drain and destination occupancy, the player submits `Request`;
  the first activated host reservation from a player actually present receives
  an unowned map lease;
- later arrivals, including the host, consume replication without taking it;
- current owner leaves: host grants the earliest serialized request from a
  remaining player, with actor ID only as an impossible-order tie-break;
- current owner disconnects: the lease is revoked without waiting for a
  hand-off request;
- no players present: host advances only the required off-map schedule state.

For ordinary villagers, "advance" should initially mean keeping the host's
shared time and durable village/Thing fields canonical, then allowing retail
AI to select the appropriate home/work/pub state when the map next becomes
live. Explicit quest travel remains a semantic scripted action and commits the
NPC's destination map as a host-validated lifecycle mutation.

Every grant carries an epoch. A revoked owner may no longer publish actions or
movement under the old epoch. When authority changes, the new owner receives a
canonical entity baseline and continues from the latest accepted action state.
Peer replacement also advances a peer-set revision, so a reconnect receives
fresh authority and lifecycle baselines even when the number of connected
players did not change.

The host entering a guest-owned map does not advance the epoch. This avoids a
full live AI/presentation handoff merely because another observer arrived. A
real owner departure still drains or explicitly aborts active exclusive
actions, advances the epoch, and publishes the canonical baseline. Stale
packets are discarded by the fence.

The coordinator retains the existing owner while that actor remains in the
map. Player-state arrival alone can neither acquire nor revoke a lease. During
a crossing, `Prepare` does not revoke the source lease; it only records the
host-serialized destination order and publishes the host's bounded
`CSavedEntities` record. The later activated `Request` remains ordered behind
source-map lifecycle transfers on the same reliable stream. Only when the
player is present in the destination may the host release or reassign the
source epoch and activate the reserved destination order. A missing actor means
disconnect rather than transition and revokes immediately.
The client also holds a final source-drain barrier after the destination world
binds: if transport backpressure leaves any lifecycle message unqueued, it
defers the destination request and retries the drain. Queue pressure therefore
cannot invert transfer-before-handoff ordering.

Simultaneous boundary crossings are serialized atomically by the host, never
elected on the clients. Each first `Prepare` for a destination map ID receives
one host arrival order. Repeating it as the post-load `Request` preserves that
order rather than entering a second race. The earliest activated eligible
reservation wins an unowned map, while an already-owned map remains sticky.
The host emits the only grant with a newly advanced map epoch, and all peers
reject older-epoch movement, lifecycle, and action messages. Two clients can
therefore cross together without producing two valid owners or prematurely
dropping either source lease.

An NPC crossing follows `source high-sim -> host low-sim -> destination
high-sim`. The source commits one final transform and destination map ID. The
host keeps the same UID/generation as a dormant, awaiting-materialization
record, assigns the current destination lease if one exists, and otherwise
keeps it ownerless. The destination owner's canonical-roster gate includes that
record and materializes it before or immediately after high-sim activation.
Late source teardown is rejected by its old map/epoch fence.

When the last occupant disappears, the host reconciles persistent records to
ownerless dormant state and retires transient records. The next first occupant
receives materialization requests for that canonical dormant roster, even when
their local hero save would not have spawned the same NPCs. This also repairs
abrupt disconnects that cannot deliver a final native Mapwho teardown.

### Per-entity action lease

Map ownership supplies the default action owner. Specific actions may receive
a narrower lease:

- dialogue: the initiating player owns an exclusive conversation lease;
- combat: the host may transfer the lease to the main attacker, with hysteresis
  and a short cooldown to avoid ownership thrashing;
- scripted quest/cutscene action: host owns and coordinates all affected
  entities;
- ambient behavior: current map owner owns it.

The host remains final arbiter. A client submits an action intent; the host
accepts it, assigns the fenced lease, and distributes the authoritative action
begin. The owner then publishes bounded updates and an action end/outcome.
An action lease is released if its owner disconnects, leaves the entity's map,
or loses the underlying map lease. Any queued native/semantic action state is
pruned at the same fence so it cannot resume under stale ownership.

The verified player `ATTACK -> CThingCreature::UseAbility` boundary now creates
a short `PlayerAttackEngagement` lease for the selected target. Repeated attacks
refresh it and idle expiration releases it. This primary-attacker lease outranks
ambient, conversation, and ordinary combat ownership but remains below
quest/cutscene control. Its owner publishes NPC locomotion through the same
interpolation/extrapolation stack as every other entity; combat movement does
not have a separate snapping path.

Primary-attacker handoff uses hysteresis rather than a fixed owner preference.
The current attacker receives a one-second minimum hold; after 750 ms without
accepted attack activity, a competing attacker can receive a new action epoch.
Denied local intents remain retryable, and accepted action updates refresh the
host lease so active combat does not hand off spuriously.

### Action messages

Replication should describe the native action rather than its incidental
memory layout:

```text
ActionBegin  entity, action id, action kind, parameters, owner, lease epoch
ActionUpdate entity, action id, meaningful progress or changed parameters
ActionEnd    entity, action id, completed/cancelled/failed, durable outcome
```

Movement-bearing actions additionally use the existing replicated movement
smoother for position, yaw, linear velocity, and angular velocity. This keeps
remote locomotion and rotation smooth while action state explains why the NPC
is moving.

Not every native action needs a custom codec on day one. The universal action
hook can identify the RTTI class and collect diagnostic samples. Verified
classes then receive explicit semantic codecs. Unknown actions remain
host-owned and can initially fall back to transform correction instead of
serializing unsafe object memory.

## Conversation behavior

Dialogue is an exclusive replicated action, not a UI-only event.

1. A player requests interaction with an NPC.
2. The host grants a conversation lease if the NPC is available.
3. All peers mark that NPC non-interactive for the lease duration.
4. The initiator runs the normal conversation, including the retail camera and
   choice UI.
5. Observers replay NPC facing, speech, gestures, and conversation animations
   through native action APIs.
6. Observers do not enter the dialogue camera or choice UI.
7. Completion, cancellation, disconnect, death, or map unload releases the
   lease and restores interaction.

The binary contains a native `NoDialogCam` script command. Its parsed boolean
is forwarded to game-script interface vtable slot `0x614`, which gives us a
retail camera-suppression path for observer replay. `CCreatureAction_Talk`,
`CCreatureAction_PlayConversationAnimation`, `CActionTalkBase`, and
`CActionTalkToThing` provide the speech and animation action paths.

`CCreatureAction_Talk` is a `0xBC`-byte action whose target Thing reference is
held as a counted pointer at `+0xB4`. Its execution resolves the target's
`CTCTalk` component (`0x42`) and starts native talk behavior. This gives the
action codec a direct participant UID without guessing from camera state.
`CCreatureAction_PlayConversationAnimation` is also `0xBC` bytes and carries
its concrete animation payload at `+0xB4/+0xB8`; those fields still need
runtime labeling before they become a wire contract.

Dialogue payloads should use conversation/line identifiers and participants,
not captured audio. An observer can then let the game resolve localized text,
voice, timing, facial animation, and gestures from its own installed data.

The slot target is preferred address `0x01C8ADF0` (RVA `0x0188ADF0`). It stores
the inverse of the passed flag in the retail dialog-camera boolean, so passing
`true` implements the script command's no-camera behavior. The client now
resolves this exact vtable function through the validated game interface;
observer replay still needs a runtime test to determine whether the flag is
latched at conversation start or must remain set for the whole action lease.

## Required client boundaries

The implementation should remain split by responsibility:

- `Game/Entity`: Thing UID, live incarnation binding, map presence, lifecycle.
- `Game/Creature/Actions`: native action observation, classification, and
  semantic codecs.
- `Game/NPC/Conversation`: interaction availability and observer-safe replay.
- `Multiplayer/Entities`: host persistent directory and live entity registry.
- `Multiplayer/Authority`: map leases and per-entity action leases.
- `Multiplayer/Replication`: spawn/retire, presence, action, state, and
  movement channels.
- `Multiplayer/Persistence`: host-only projection of accepted shared-world
  state into retail save seams.
- `Multiplayer/Protocol`: typed packet envelope rather than a player-state-only
  transport.

`MultiplayerSession` should only compose these systems. It must not absorb NPC
identity, action, movement, conversation, and persistence policy itself.

## Implementation status

Implemented in the current development tree:

1. Protocol-v26 typed reliable authority, lifecycle, map-roster, population,
   action, and health messages plus lossy entity movement messages.
2. A bounded host entity directory keyed by canonical Thing UID and generation.
3. Sticky host-serialized map-request leases, ordered source-to-destination
   handoff, and empty-map dormancy reconciliation.
4. Ordered Mapwho lifecycle observation, complete owner-roster boundaries,
   removal of noncanonical local NPCs, source-map transition drain, final
   transfer transform capture, and destination materialization with UID
   aliases. Ordinary spatial unregister/re-register cycles retain the alias;
   only terminal component destruction releases it.
5. Reuse of the hero-quality interpolation/extrapolation locomotion stack for
   NPC translation and yaw.
6. Universal creature-action diagnostics, primary-attacker combat leases, and
   native AI/action-update/action-submission fencing by entity publisher.
7. Host-only Albion population simulation and lease-owner-only high-detail
   population simulation.
8. Host map-ID projection at retail Thing load/save seams, including unique
   script-identity adoption for stale save-derived incarnations.
9. Known-map canonical-roster bootstrap on lease handoff, with native AI,
   population, actions, movement, and lifecycle publication held until the
   successor's exact local roster matches the host boundary.
10. Explicit host-issued seed permission for genuinely unseen maps, fenced to
    one owner and epoch so a lease/baseline race cannot publish a stale save.
11. Four-region host low-sim target replication, applied to the lease owner's
    retail high-detail callback before random NPC spawn/exit decisions.
12. Bounded binary/text `CSavedEntities` map-blob capture plus a validated
    binary guest installer that uses the game's own record/vector routines.
13. Chunked, hashed host saved-map baselines ordered ahead of every map grant;
    guests retain one current revision per map and reapply it after save load.
14. Two-phase `Prepare`/`Request` map acquisition. The host atomically records
    simultaneous boundary order without revoking source authority, and starts
    baseline transfer before destination activation.
15. Observation-first native travel hooks at `CTCDRegionExit` activation and
   UE3 `PrepareMapChange`, pairing the connected entrance's native map ID with
   the destination `FName` before retail world mutation.
16. Reliable absolute current/maximum health mutation replication at the
   shared `CThingCreature::ModifyCombatHealth` boundary for locally owned
   Heroes and the current map/action owner of NPCs and guards. Host revisions,
   late-join baselines, and authoritative application are bounded per player
   or canonical entity generation. One current value survives a dormant
   cross-map teardown and is restored before the destination owner publishes
   its new native incarnation.
17. Durable `CTCVillageMember::VillageUID` mutation capture and host-authority
   application before a destination creature's AI is unfrozen.
18. Map-owner `CTCDummyVillager` recreation-schedule mutation replication,
   cross-map restoration, late-join replay, and host save projection. The
   serializer reads an immutable snapshot, so Fable's file-writer thread never
   traverses mutable session state.
19. Deterministic acceptance stimuli for a damageable guard combat handoff and
   a source-Mapwho-to-destination-materializer NPC crossing. Authority coverage
   includes simultaneous destination requests, sticky host re-entry, and owner
   disconnect recovery; the fixtures use ordinary production replication after
   producing the exact native input boundary.

Still required:

1. Runtime confirmation that ordinary Fable region exits hit both native travel
   hooks, that their map ID/name pairing matches the post-load Hero identity,
   and that typical baseline sizes arrive before destination construction.
2. Run the implemented acceptance for NPC crossing, sticky host-late-entry
   behavior, simultaneous crossing, attacker handoff, owner disconnect, and
   stale-packet fencing against the current protocol-v26 DLL.
3. Measure the existing stop-and-wait reliable lane with real saved-map record
   sizes; widen or window baseline transport only if transition timing requires
   it.
4. Run the damageable-guard acceptance for the implemented typed player/NPC
   health mutations and native animation replay, and verify recreation state
   survives a guest-owned map save/handoff. Then add further combat outcomes
   and semantic action codecs.
5. Exclusive conversation replay with observer-side camera suppression.
6. UID-aware canonical directory persistence across host restart.
7. Availability, quest, store, and other shared-world mutation codecs.

Runtime acceptance must cover two players in one map, separate maps, NPC map
crossing, host entry into a guest-owned map, concurrent interaction attempts,
combat ownership transfer, disconnect during an exclusive action, and stale
packets from a revoked lease.
