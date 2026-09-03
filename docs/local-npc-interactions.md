# Local NPC interactions

Status: crash guards and local-economy isolation implemented; Release Win32
build and player-replication tests pass. The user confirmed the two-real-save
manual test was stable on 2026-09-03 (run `20260903-131707-718-9344`).
Simultaneous shopping/save-economy checks were not separately confirmed.
Exceptional-arrival shop-link repair is not implemented; missing links remain
deferred.

## Ownership

Each player must be able to shop independently, including at the same merchant
at the same time. Reuse retail shopping and the player's own save economy.

| Data or behavior | Owner |
| --- | --- |
| NPC identity, existence, map, shared movement/combat | Host world and current map simulation owner |
| Shop/component initialization and local object references | Each process, from the shared NPC baseline |
| Shopping UI, prices/stock changes, purchases, gold and personal inventory | The local player/save |
| Quest progression or other shared consequences of an interaction | Host, through the existing authority path |

Local dialogue/UI does not grant authority to move the NPC, damage another
actor, advance a quest, or write changes into another player's economy.

## Why the received save is not sufficient yet

The failing guest created a `CREATURE_BOWERSTONE_SMITH` through the exceptional
arrival path, then its native `CAIStateGroup_SetupWares` predicate dereferenced
an invalid intelligent-pointer header. The minidump does not contain the
component heap, so the origin of that invalid reference is not proven.

`EntityMaterializationService::Spawn` currently supplies the creature definition,
transform and script name, then binds network identity. That is not evidence
that a live shopkeeper has its shop, ownership and wares references linked.
Those references must be reconstructed locally, never copied as host pointers.

There is also a separate native state-group decision path that bypasses the
current CAIBrain update hook. A blanket ownership block there would prevent
legitimate local setup; allowing it unconditionally retains the current risk.

## Integration contract

1. **Finish native setup before enabling interaction.** Extend the existing
   materialization lifecycle with actor-scoped interaction readiness. Adopt the
   correctly loaded native shopkeeper when available; otherwise use the native
   setup/linking sequence after its shop dependencies exist. Verify current
   component layouts and setup ordering first. On missing dependencies, defer
   this shop interaction, log the reason once, and retry on lifecycle changes.
   Do not block the whole map, substitute an unrelated NPC, or swallow faults.

2. **Separate local setup from autonomous decisions.** Reuse
   `EntitySimulationAuthority` at the uncovered state-group boundary. Classify
   only verified setup/interaction paths as local and require interaction
   readiness; ordinary schedules, roaming and combat remain owner-only.
   Inspect `SetupWares` beyond its predicate: if it also moves the merchant,
   isolate its local wares initialization rather than authorizing the entire
   group. Keep native locomotion, animation and replicated actions running.

3. **Keep retail transactions private.** Run the native transaction against
   the real local Hero and local merchant economy. Shopping must not acquire
   an exclusive shared dialogue lease, submit a shared turn/follow action, or
   publish purchases and display wares as world mutations. Local interaction
   presentation must not overwrite the NPC's authoritative shared action.
   Quest-changing dialogue still follows the shared quest authority path.

4. **Preserve the local economy across host updates and saves.** Identify the
   native fields/sections for merchant stock, prices and transaction history.
   Preserve the matching fields from the selected local save, then overlay only
   those private values after a host world baseline. If a merchant has no local
   entry, seed from the host's valid initial shop state once. Preserve the
   existing exact local Hero inventory/gold boundary. Never restore an entire
   guest NPC or map cell to preserve shopping data. If native serialization
   cannot isolate these fields, use a small typed per-save merchant delta file;
   decide that only after tracing the native save format.

Reuse stable native/simulation identity, with an exact unique map/script/
definition match only when required; never nearest-position matching. Private
economy data needs a campaign/save scope so unrelated worlds or replacement
merchants cannot inherit each other's stock. Keep only active actor readiness
in memory and bounded current merchant deltas, not an event history.

Use the existing authority, materialization and save-projection components.
One focused native shop adapter is sufficient if needed; no new networking
protocol, general rule engine, or all-purpose interaction coordinator.

## Implemented boundaries

- `AiBrainUpdateObserver` covers both the existing brain update and the direct
  creature state-group dispatcher. Both use `EntitySimulationAuthority`; no
  second ownership table or blanket SetupWares exemption was added. The full
  SetupWares group also performs shared wares-placement/action work.
- `ShopKeeperSetupGuard` validates the current local shopkeeper/shop graph
  before the native SetupWares predicate runs. Missing components or invalid
  weak references return not-ready, with bounded diagnostics. Readiness is
  read afresh on the next native attempt, so no stale actor pointer is cached.
  Native constructors/OnCreate remain responsible for linking and initial stock;
  the mod does not synthesize components or repeatedly force OnCreate. An
  exceptional arrival that retail never links remains deferred, not repaired
  by a speculative native call.
- Trade actions stay out of shared action replication and exclusive dialogue
  leases. Native shopping UI, pricing and Hero inventory/gold are retained;
  movement, combat and quest consequences keep their existing authority rules.
- `LocalShopSaveBoundary` preserves only the serialized `CTCShop` component
  from the selected save and subsequent native map records. Matching requires
  exact map ID, UID, entity class and definition within that selected-save
  scope. Other components and opaque entity trailers remain host-owned.
  Missing local stock starts from the host baseline. No new save sidecar.
- Private shop data is projected into a temporary installation copy, never
  retained inside the authoritative host baseline. Capture runs during native
  save loading and immediately before a host baseline replaces map records,
  not every frame. Unchanged map data skips decompression. The cache is bounded
  to 4,096 merchants/16 MiB of components; individual native cells are capped
  at 8 MiB. Unsupported shop projection logs a diagnostic and leaves the host
  record unchanged; it cannot hold the whole map load indefinitely.
- New inline detours use the shared `InlineHook` primitive with validated
  current-build targets. Their callback DLL and trampolines remain resident;
  shutdown detaches policy while preserving native passthrough. Authority
  callback leases drain before their owning service is released.

Validation includes exact extract/reinsert round trips on three real saved-map
samples (one, zero and three merchants), parser bounds/opaque-trailer tests,
private-economy isolation tests, authority tests and executable-stub tests of
the production detours. These do not substitute for retail gameplay acceptance.

## Acceptance

- Guest enters Bowerstone while the host is in the introduction cutscene,
  after the scene, and on repeated leave/reentry: no crash or permanent load
  gate; correctly initialized merchants on both peers.
- Both players buy/sell from the same merchant simultaneously. Each player's
  gold, items and merchant stock change only locally, with native pricing.
- Save/reload with different real saves, refresh the host baseline, and change
  map ownership: private economy persists; shop references are rebuilt for the
  new incarnation; stale readiness is cleared on teardown.
- Merchant movement/combat remain synchronized. Local shopping does not spawn
  duplicate NPCs/wares or advance host quests accidentally.
- Delay or omit the shop dependency: interaction waits safely and becomes
  available after native setup; it never calls SetupWares on a partial graph.

Focused tests cover the readiness/ownership decision and private-save overlay.
The Bowerstone reproduction and simultaneous shopping require in-game manual
confirmation; a successful build alone does not establish the crash is fixed.
