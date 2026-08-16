# Initial Fable Anniversary multiplayer hook analysis

## Executive conclusion

The proposed RP multiplayer mode is feasible as an injected, server-authoritative
extension. The strongest first route is not to revive Fable's dormant UE3
replication wholesale. It is to use a separate mod protocol, discover and mutate
Fable objects on the game thread, and give the server authority over actors and
persistent world state.

The executable and shipped data contain concrete surfaces for:

- actor/controller/pawn discovery, position, rotation, animations, and actions;
- appearance seeds, morphs, clothing, hair, tattoos, and NPC creature types;
- NPC nameplates, quest HUD, quest log, GFx menus, and the existing trade pages;
- scripted conversations and custom interactions;
- quest registration, activation, objectives, lifecycle, and rewards;
- doors, damageability, health modification, ownership identifiers, and travel;
- Wwise listener and emitter positioning for optional later voice integration.

The most delicate requirement is the questless open world. Fable uses background
and story quest scripts to control more than quest-log progression: they can also
select NPC populations, barriers, doors, scenes, and post-quest world branches.
The correct target is therefore **no visible vanilla story quests plus a canonical
RP world-state profile**, not a blanket hook that reports every quest completed.

## Target identity

- Installation: `D:\SteamLibrary\steamapps\common\Fable Anniversary`
- Executable: `Binaries\Win32\Fable Anniversary.exe`
- Architecture: PE32 / x86
- Image base: `0x00400000`
- Image size: `0x035D5000`
- Entry point: `0x0276A782`
- SHA-256: `2a95eea3c2cce9b47ca0f454a605b6952216f5d25158efd12ba48b70130989f2`
- Original build name: `WellingtonGame-Win32-Shipping.exe`
- PDB identity: `{D6C268DF-C454-455C-8C90-FDFF4334B86D}`, age 1

Every runtime hook must gate on this original executable hash and then validate a
byte signature and surrounding semantics. Addresses in this document are RVAs in
the preferred image at base `0x00400000`; ASLR-aware code must use module-base
relative offsets or signatures.

The shipping executable is mixed native/CLR. IDA automatically selects its CLR
view, which exposes only a small managed surface and conceals the native UE3
image. The useful database was made from an analysis-only copy whose CLR
data-directory entry is cleared. The original executable was not patched.

## Evidence and limitations

The initial pass used the native executable image, `WellingtonGame.u`, UE3 script
manifests, configuration, and the shipped `FableData` developer/legacy files.
The native IDA database is targeted rather than a completely named decompilation:
strings and selected registration/dispatch regions were analyzed first.

The readable files below are particularly important:

- `WellingtonGame\FableData\Build\Data\Levels\FinalAlbion.qst` registers the
  game's global, story, vignette, optional, repeatable, activation-helper, dummy,
  and test quests through `AddQuest(...)` and `AddTestQuest(...)`.
- `WellingtonGame\FableData\Build\Data\Levels\GlobalQuests.qst` registers
  background scripts such as hero-death handling, expressions, item rewards,
  chest opening, and debug travel.
- `WellingtonGame\FableData\Build\Data\Levels\Ini\*.end` files are readable
  scripts. For example, `StandardScript.end` grants experience and money, while
  `AmbushTraders.end` also deactivates phase quests.
- `hero_abilities.h` enumerates the trainable Strength, Skill, and Will stats and
  individual abilities. `HL0.INI` through `HL7.INI`, `full_skill.ini`, and
  `no_skill.ini` use direct per-stat level setters and an all-spells grant.
- `GuildTraining.ini` uses `TurnPlayerInto(...)`; region/name tables enumerate
  guard creatures, prison factions, prison rooms, cell keys, and other quest-key
  objects.

These files prove the original quest authoring model and match native runtime
symbols. They do **not yet prove** that Anniversary loads a modified loose `.qst`
or `.end` file in preference to cooked data. A file-open trace or an isolated
throwaway quest registration test is required before treating loose-file editing
as a supported extension mechanism.

## Concrete native anchors

Addresses below are discovery anchors, not final detour sites unless explicitly
validated. A string address identifies reflected metadata; its nearby code site
is often the useful signature anchor.

| Surface | Anchor | Initial interpretation |
| --- | --- | --- |
| Fable class registration | `sub_2027C80` at `0x02027C80` | Registers many Fable native/script-facing classes. |
| `WellingtonPawn` | reference at `0x02027F69` | Followed by class setup at `0x0202A780`. |
| `HeroPawn` | reference at `0x02027FDB` | Followed by class setup at `0x02031870`. |
| `WellingtonPlayerController` | reference at `0x0202802D` | Followed by class setup at `0x0202AA20`. |
| `HUD_NPCName` | reference at `0x020289C5` | Followed by class setup at `0x02034940`; promising overhead-name route. |
| `UI_PageTrade` | reference at `0x0202A1EB` | Followed by class setup at `0x0200F5F0`; existing trade presentation. |
| `GFxMoviePlayer` | `sub_E8ECC0` at `0x00E8ECC0` | GFx/Scaleform class setup and UI discovery anchor. |
| Ownership field | `sub_1E6EC00`, `OwnerUID` ref at `0x01E6EC9A` | Candidate stable object/property ownership identifier; semantics still need runtime validation. |
| Appearance | `.SetAppearanceSeed` ref at `0x01764978` | Script/native dispatch anchor for deterministic appearance. |
| Creature replacement | `GetHero` at vtable index 70 / `0x01C89940`; `TurnCreatureInto` at index 100 / `0x01C98200` | Anniversary-native path used by the single-player `1`-key transformation probe. The corresponding RVAs are `0x01889940` and `0x01898200`. |
| Hero morphs | `CHeroMorphDef`; `Strength`, `Skill`, `Morality`, `Fatness`, `Teenager`, and `HairModifierNames` metadata near `0x02EF7888`-`0x02EF7D30` | Stable evidence for semantic stat/body/hair synchronization; the exact old-age channel still needs a live trace. |
| Animation/action | `.PlayAnimation` ref at `0x01760E1C` | Script/native dispatch anchor for semantic action playback. |
| Door state | `SetDoorOpen` push at `0x01768FBF` | Dispatch anchor for authoritative open/closed state. |
| Combat health | `.ModifyHealth` push at `0x01761CBF`; interface target `0x01C8DE50`; `CThingPlayerCreature` vtable slot `+0x100` target `0x01F5A520`; shared `CThingCreature::ModifyCombatHealth` target `0x01F59CB0` | Confirmed common player/NPC health-mutation path. All creatures store maximum HP at `+0xCC` and current HP at `+0xD0`; the player override only rounds its delta before delegating to the shared function. |
| Damage policy | `.SetDamageable` push at `0x0176099F` | Dispatch anchor for proxy/authority damage rules. |
| Hero progression "Health" | `GiveHeroHealth` push at `0x01766459`; target `0x01C9DA40` | Component type 4 progression value at `+0x30`, with maximum from `0x01DCC6DA`. This is not combat HP. |
| ScriptThing transform | position helper `0x0175B93F`; facing helper `0x0175B994` | Live read-only transform seam confirmed against the loaded Hero. |
| Script teleport | interface vtable `+0x7B0`; target `0x01C9EE20` | Confirmed five-argument `TeleportThing` path. The retail dispatcher supplies `ScriptThing*`, float3 position, facing, `false`, and `0`; collision may correct Z to ground height. |
| Active region | region manager lookup mirrored from `TeleportThing`; index resolver `0x01FC6560` | Resolves the current position to a stable one-based engine region index. The bootstrap fixture is region `4`; snapshots assert this identity and reject cross-region teleport. |
| Quest registration parser | `AddQuest` refs at `0x01FA78CE`, `0x01FA7934` | Code consumes the same command present in the loose `.qst` manifests. |
| Test-quest parser | `AddTestQuest` refs at `0x01FA7901`, `0x01FA7C00` | Confirms structured test quest registration support. |
| Quest activation | `ActivateQuest` push at `0x01CB993A` | Native/script activation anchor. |
| Quest manager | log-string push at `0x01FC1BE2`; `CQuestManager` RTTI | Confirms a central quest manager and activation path. |
| Quest definition | `ScriptQuestName` push at `0x01E7D36D` | Quest-card/definition field tying UI objects to script quests. |
| Quest rewards | `GoldReward`, `XPReward`, `ItemReward` pushes at `0x0177BD42`, `0x0177BD5B`, `0x0177BD74` | Structured reward fields in quest data. |
| Reward objects | `RewardObjects` push at `0x0178BADC` | Object-reward collection path. |
| Quest lifecycle | `QuestSucceeded` / `QuestStarted` pushes at `0x017EA1DE` / `0x017EA210` | Observable quest state transitions. |
| Hero experience | `ExperiencePointsAvailableToSpend`, `StatExperiencePoints`, and `TrainableStatLevels` refs near `0x01DBBD68`-`0x01DBBD8C` | Candidate interception surface for a server-owned XP curve and purchase validation. |
| Ability levels | `AbilityLevels` refs at `0x01DECFA4` and `0x01DED38E` | Candidate surface for server-granted and activity-gated abilities. |
| Slow Time | `HERO_ABILITY_TIME_SPELL` and inventory-thing metadata; latter referenced at `0x01DE6823` | Gives the banned ability an explicit definition to strip, hide, reject, and prevent from casting. |

Additional reflected engine calls include `AActor::execSetLocation` and
`AActor::execSetRotation`. The next native pass should resolve their exec
implementations and `UObject::ProcessEvent`, then use those names to observe live
objects before selecting permanent hooks.

The active Hero resolves to RTTI type `CThingPlayerCreature` with preferred
vtable `0x02F1DBB4` (RVA `0x02B1DBB4`). Its combat-health fraction helper at
`0x01F58C80` returns `current(+0xD0) / maximum(+0xCC)`, and the maximum setter at
`0x01F58E10` updates `+0xCC` then clamps `+0xD0`. The multiplayer observer is
installed on the common creature mutation at preferred `0x01F59CB0` (RVA
`0x01B59CB0`) so Hero, guard, and NPC damage follows the same path. The
multiplayer vitals channel publishes reliable absolute current/maximum values
at that mutation boundary: each player authors their own Hero, while the
current entity publisher authors guards and NPCs; the host validates and
revisions both. Automated run
`20260807-193935-836-24140` used the normal vtable `+0x100` mutation path to
change HP from 20 to 17 and the script `TeleportThing` path to apply a distinct
server spawn, then verified both for three ticks.

Appearance evidence also includes `CAppearanceDef`, `CAppearanceModifierDef`,
`CHeroMorphDef`, `CTCHeroMorph`, `HeroHair`, `HeroTattoo`, and clothing dispatch
names. Action evidence includes `CActionPlayAnimation` and many
`CCreatureAction_*` classes.

## Feasibility by capability

| Capability | Feasibility | Recommended first implementation | Principal risk |
| --- | --- | --- | --- |
| Player transform and locomotion | High | Sample the local hero on the game thread; send snapshots; interpolate a non-possessed remote pawn/proxy. | Level streaming invalidates cached pointers. |
| Player actions and animation | High | Replicate semantic action IDs and timestamps, then map them to Fable action/animation dispatch. | A visual action must not trigger local damage or quest logic. |
| Server-authoritative NPC position/action/availability | Medium-high | Run real NPC AI only in the host simulation; clients receive same-region proxy snapshots and spawn/despawn state. | A truly dedicated headless simulation is a later, harder milestone. |
| Appearance synchronization | Medium-high | Replicate appearance seed plus stable definition/modifier IDs, never raw mesh pointers. | Hero morph state and NPC definitions may use different pipelines. |
| Stat, equipment, hair, and tattoo appearance | High | Replicate a semantic appearance profile and apply hero morph plus attachable appearance modifiers. | Definition IDs must be mapped per executable/data version. |
| Scheduled age and retirement | Medium-high as policy; medium for exact visuals | Store age server-side; map it to native morph/hair modifiers; make retirement an explicit RP lifecycle event. | Exact wrinkle and greying controls still require a live trace or authored fallback modifiers. |
| Play visually as an NPC | High | Retain a hidden/logic `HeroPawn` and drive an NPC visual proxy or replacement appearance. | True NPC possession is only medium/low confidence because game code may cast to `HeroPawn`. |
| Slower XP and higher stat costs | High | Server validates every purchase against a custom curve, then invokes direct stat-level setters for accepted ranks. | Native UI cost display must be replaced or mirrored so it never lies. |
| Activity-gated or banned spells | High | Server owns ability grants; filter experience UI/equip lists and reject unauthorized equip/cast attempts. | Existing saves and local scripts can reintroduce abilities unless reconciled continuously. |
| Player display names | High | Reuse `HUD_NPCName`/GFx or build an equivalent GFx overlay bound to entity IDs. | Occlusion and projected-screen placement need polish. |
| Custom trade menu and player economy | High | Reuse trade-page conventions; server owns quotes, inventory, currency, escrow, and atomic commit. | Never trust client-calculated prices or inventory. |
| Server-owned NPC stores | High | Bind merchant IDs to server inventory/price rules and use existing trade UI only as presentation. | Local merchant stock calculation must be bypassed. |
| Property ownership | Medium-high | Map a persistent thing key/validated `OwnerUID` to server ownership and ACL records. | `OwnerUID` stability must be tested across reloads and regions. |
| Door allowlists | Medium-high | Validate interaction server-side; broadcast versioned door state; invoke `SetDoorOpen` on clients. | Quest scripts may attempt to overwrite the door state. |
| Enemy health/combat | Medium | Host owns NPC AI, hit validation, damage, death, drops, and health revisions; clients render results. | Prevent duplicate local damage and divergent hit reactions. |
| Custom conversations | High with mod UI; medium with native voiced dialogue | Server-owned dialogue tree presented through GFx; bind choices to custom interaction IDs. | Native localization/audio/lip-sync authoring is more constrained. |
| Questless but physically traversable world | Medium | Suppress visible story quests and apply a tested RP world-state profile to gates, regions, boats, and NPC availability. | Quest completion branches also mutate population and scenery. |
| Server daily/custom quests | High with server lifecycle; medium for full native quest injection | Server owns definitions, counters, expiry, and rewards; mirror presentation through custom UI or a generic native bridge quest. | Loose `.qst` runtime loading and arbitrary dynamic text are not yet proven. |
| Approved guard shifts | High for role and appearance; medium for arbitrary creature replacement | Apply a server duty role and guard visual profile while retaining HeroPawn logic for the first version. | Full `TurnPlayerInto(guard)` may break hero-specific assumptions. |
| Arrest, jail, and warden roles | Medium-high | Convert an authoritative defeat into custody, assign a prison cell/sentence, and enforce prison travel and doors server-side. | Must bypass ordinary death/save/drop behavior without leaving partial state. |
| Physical tradable keys | High with a server item model; medium for native lock binding | Represent each key as a unique server item and reuse Fable key objects/door presentation. | Native keys appear quest/script-driven rather than one generic lock framework. |
| Proximity voice | High | Separate Opus media channel keyed to mod entity IDs; spatialize from replicated transforms. | Jitter, mute/moderation, and room/door occlusion policy. |

## Questless RP world model

“No quests” should mean no active or visible vanilla story progression. It cannot
mean disabling the quest manager or pretending every quest is complete at every
call site. `FinalAlbion.qst` shows always-on scripts such as
`ChapterAndSceneManager`, `PersonalScriptMain`, `PersonalScript_GlobalThings`,
`NPCDeath`, and `HeroBoasts`; other quest helpers explicitly manage barriers,
gates, creature generators, and region-specific scenes.

The server should publish a versioned RP world profile containing:

- the intended region/scene branch for every playable area;
- NPC spawn/availability overrides;
- door and barrier state plus ACL policy;
- enabled physical travel links such as roads, region portals, boats, and
  NPC-mediated transitions;
- suppressed vanilla quest cards, prompts, fast travel, and story triggers.

Some valid physical transitions will still use Fable's internal teleport or HSP
mechanism when moving between disconnected regions. The important distinction is
that the transition starts from a validated nearby door, boat, road exit, or NPC
interaction. The client never supplies an arbitrary destination.

The server tracks every player globally but only streams actor snapshots for the
same region/instance. A transition request should contain the interacted world
object ID, current region, destination link ID, position proof/tolerance, and
world-state revision. The server resolves the destination and broadcasts the
region change.

## Authoritative NPCs, conversations, and stores

The native image contains `CThingPlayerCreature::Create`, creature-generation
enable/disable data, numerous creature action classes, `CScriptConversation`,
`scriptdialogue`, and dialogue identifiers. The UE3 layer also exposes
`SetCustomInteractionObject`, `GetCustomInteractionClass`, and
`GetNumCustomInteractions` on the game viewport client.

For the first host-authoritative version:

1. The host owns each NPC's stable entity ID, definition, region, transform,
   action, appearance, health, availability, and current interaction lock.
2. Only the host runs that NPC's AI and damage decisions.
3. Guests create non-AI visual proxies and apply ordered snapshots on the game
   thread.
4. Conversation selection, quest acceptance, store opening, and travel choices
   are requests to the server, not local state transitions.

Custom dialogue is most reliable as server data rendered by a GFx page. It can
reuse existing localization/speech identifiers where available, while allowing
new unvoiced text and choices without rebuilding Lionhead dialogue assets. A
later pass can determine whether native `CScriptConversation` assets can be
authored with acceptable audio, lip-sync, and localization tooling.

`CShopItemDef`, shopkeeper/trader stock strings, and the `UI_PageTrade*` family
confirm that existing store presentation can be reused. The merchant server
record should contain an availability revision, item stacks, buy/sell formulas,
currency, and restock schedule. The client requests a signed/versioned quote;
the server performs an atomic inventory/currency transaction and then publishes
the new merchant and player state.

## Custom and daily quest assessment

Custom daily quests are practical, but their **authority must remain in the mod
server** even if Fable's quest UI is reused.

The shipped quest system already has the right concepts:

- named script quests registered with active/inactive defaults;
- explicit activation/deactivation and started/succeeded/failed states;
- quest-card definitions and `ScriptQuestName` links;
- objective text, counters, timers, tick/checkmark elements, quest categories,
  current/past/available quest pages, and start/completion/failure presentation;
- structured gold, XP, item, and object rewards;
- repeatable (`QR_*`), optional/vignette (`V_*`), helper/activation, dummy, and
  test quest patterns.

The existing HUD/UI classes include `HUD_QuestCard`, `HUD_QuestInfo`,
`UI_PageQuestsCurrent`, `UI_PageQuestsPast`, `UI_PageQuestsSelection`, and
`UI_PageLogbookQuests`. This makes native-looking quest presentation very
plausible.

There are two implementation tiers:

1. **Server quest with custom GFx presentation — high confidence.** Define daily
   quests in server data, observe server-authoritative gameplay events, update
   counters/revisions, and grant materials or currency atomically on the server.
   This does not depend on Fable accepting new loose quest assets.
2. **Native quest bridge — medium confidence until runtime-tested.** Register one
   generic mod quest/card or populate existing quest HUD data and mirror the
   selected server quest into it. Avoid creating a new native quest name every
   day; repeated dynamic registrations may leak name-table entries or pollute
   saves. The bridge must never execute a local `.end` reward as authority.

A daily quest definition can be as small as:

```text
quest_id, version, title, description, objective_type,
target_definition_or_region, required_count, starts_at, expires_at,
reward_items, reward_currency, repeat_policy
```

Player progress should be keyed by account/mod identity and server quest version.
Useful first objective types are server-observable and deterministic: kill a
specific creature definition, deliver a material to a server merchant, visit a
region through a valid travel link, perform a trade, or interact with a named NPC.
Completion must be idempotent so reconnects cannot duplicate rewards.

The next quest-specific experiment should intercept the `AddQuest` parser and
`CQuestManager` activation path during startup, record which `.qst` source is
consumed, and inject a single disposable quest with no reward or save mutation.
Only after that succeeds should we decide whether the bridge uses loose assets,
runtime object construction, or custom GFx exclusively.

## Progression, abilities, and visible aging

Progression can be made substantially slower without depending on the vanilla
purchase rules. The shipped definitions enumerate every trainable stat and
ability, and the developer level scripts call individual setters such as
`SetHeroStrengthPhysiqueLevel(...)`, `SetHeroSkillSpeedLevel(...)`, and their
Health, Toughness, Accuracy, and Stealth counterparts. The executable also has
distinct experience pools, spent-experience bookkeeping, trainable-stat levels,
ability levels, and experience-menu classes.

The robust design is for the server to own XP balances, rank costs, prerequisites,
and unlock events. A purchase request names a semantic stat/ability and desired
rank; after validation the client applies the corresponding native setter. The
experience UI must display the server quote, not a locally calculated vanilla
cost. This supports slower advancement and special-activity unlocks while keeping
Fable's native stat and spell presentation.

Slow Time has an explicit `HERO_ABILITY_TIME_SPELL` definition and inventory
thing. Banning it should be defense in depth: never grant it, strip it from
imported saves, hide it from purchase/equip lists, block local equip/cast paths,
and reject it in the server protocol. Merely changing its XP or casting cost is
not sufficient.

Appearance should be synchronized independently from secret combat/stat values.
`CHeroMorphDef` exposes Strength, Skill, Morality, Fatness, Teenager, and hair
modifier metadata, alongside attachable appearance modifiers, clothing, hair,
beard, and tattoo definitions. Replicate a semantic `AppearanceProfile` containing
definition IDs and normalized morph inputs. Age can be a server clock (for
example, one character year per real week), but the exact old-age wrinkle/grey
channel has not yet been identified. If the native age morph is not independently
controllable, an authored texture plus grey hair/beard modifier is a safe visual
fallback. Retirement remains an RP policy and should not silently delete a
character.

## Guard shifts, prison, and physical keys

The shipped scripts contain `TurnPlayerInto("CREATURE_HERO")` and
`TurnPlayerInto("CREATURE_HERO_CHILD")`, proving a runtime creature transform
route. Native data also enumerates Bowerstone, Knothole Glade, prison, crossbow,
and colored guard creatures. The Anniversary-native `GetHero ->
TurnCreatureInto` call path is now resolved and wired to the number-row `1` key,
with `Shift+1` reserved for restoring `CREATURE_HERO`. Actual guard and non-human
compatibility still needs a disposable-save runtime test. If full creature
replacement destabilizes hero logic or possession, the production design should
retain HeroPawn for logic and use a guard-compatible visual proxy or outfit. The
server duty permission, not the outfit, grants guard powers.

The game contains Bowerstone Jail and a larger prison complex with cells, office,
barracks, courtyard, torture chamber, paths, a secret passage, prisoner and guard
factions, a prison warden, and several prison quests. An authoritative combat
result can therefore be converted from ordinary death into `defeated -> custody`:
validate the on-duty guard and jurisdiction, cancel lethal/drop behavior, clear
combat, create a sentence, transfer through jail intake, assign a cell, and apply
restricted travel/door permissions. Warden actions and timed release are ordinary
server state transitions.

Named inventory objects include `OBJECT_PRISON_CELL_KEY`, a prison key rack,
`OBJECT_GRAVEYARD_GATE_KEY`, `OBJECT_HOBBE_CAVE_DOOR_QUEST_KEY_01`, and other
door/quest keys. Initial evidence suggests those are script/inventory driven,
rather than instances of one generic native lock-key framework. The multiplayer
model should therefore make every key a non-fungible server item whose lock set,
owner/container, issuer, copy generation, revocation, and revision are explicit.
Fable supplies the visible object and interaction; the server decides whether the
key, role, allowlist, or property ACL opens the door. This permits secret trades
without making locally duplicated inventory objects authoritative.

The complete gameplay treatment and unresolved policy choices are recorded in
the living [design ideas](design-ideas.md) document.

## UI and proximity voice

The GFx/Scaleform surface includes `GFxMoviePlayer`, `GFxObject`, `SwfMovie`,
`WellingtonMenu`, and `WellingtonInGameMenu`. `HUD_NPCName` has reflected
`FillData` and `GetShow` methods. The trade and quest UI families expose `DoBegin`,
`DoTick`, event, and data-preparation methods. That is sufficient evidence to
prioritize native-looking nameplates, dialogue, store, and quest pages.

Voice should remain a separate low-latency media plane. Steam is useful only for
account identity/presentation such as SteamID, persona name, and optionally
avatar. It should not define gameplay identity, transport, or authority.

Capture microphone audio, encode Opus frames, and attach the speaker's mod entity
ID. Receivers use the same replicated position data as avatar rendering for
distance falloff and stereo pan. Add jitter buffering, mute/moderation, speaking
indicators, and later room/door occlusion. The executable exports Wwise
`SetListenerPosition`, `SetPosition`, and obstruction/occlusion functions, but
independent playback is the safer first milestone.

## Initial protocol records

All network input is queued and applied on the game thread. Useful initial
records are:

```text
EntitySnapshot:
  entity_id, entity_kind, region_id, server_tick, transform, velocity,
  action_id, action_started_tick, appearance_descriptor,
  health_current, health_max, state_revision, available

DoorState:
  world_object_id, region_id, open, locked, allowlist_id, revision

PropertyState:
  persistent_object_id, owner_identity, acl_id, revision

MerchantState:
  merchant_id, npc_entity_id, inventory_revision, stock, pricing_policy

ConversationState:
  conversation_id, npc_entity_id, participant_ids, node_id, revision

QuestProgress:
  quest_id, quest_version, player_identity, counters, status,
  accepted_at, expires_at, revision, reward_claim_id

AppearanceProfile:
  profile_id, creature_or_outfit_definition, appearance_seed,
  normalized_morphs, hair, beard, tattoos, equipment, age_years, revision

ProgressionState:
  player_identity, xp_balances, stat_ranks, ability_grants,
  banned_abilities, progression_ruleset, revision

KeyInstance:
  key_instance_id, template_id, owner_or_container, lock_set,
  issuer, copy_generation, expires_or_revoked_at, revision

CustodyState:
  sentence_id, prisoner_identity, arresting_guard_identity, prison_id,
  cell_id, starts_at, release_at, restrictions, status, revision
```

## Recommended hook sequence

1. Signature-locate `UObject::ProcessEvent`, `GWorld`, the local player,
   `WellingtonPlayerController`, and the possessed `HeroPawn`.
2. Add a game-thread dispatch/tick bridge and a diagnostic object/event tracer.
3. Resolve the actor spawn path and construct one non-possessed remote visual
   actor in the current region.
4. Replicate transform, velocity, facing, locomotion, and one semantic animation
   with interpolation and clean despawn.
5. Add stable mod/account/entity identities and an `HUD_NPCName` nameplate.
6. Introduce host-owned NPC proxy replication and availability in one region.
7. Trace quest registration/activation and test one rewardless disposable bridge
   quest without touching the player's real save.
8. Expand independently into appearance, conversations, stores, doors/properties,
   combat, world transitions, daily quests, and voice.

## Native locomotion seam recovered on 2026-08-07

Runtime component type `0x2` is `CTCPhysicsControlled` on the real Hero and
`CTCPhysicsNavigator` on a generic NPC. Their vtables expose compatible slot-32
movement-vector setters:

- `CTCPhysicsControlled` vtable `0x02F0764C`, slot 32 ->
  `CTCPhysicsBase_SetWorldPosition` at `0x01E73480`;
- `CTCPhysicsNavigator` vtable `0x02F079AC`, slot 32 ->
  `CTCPhysicsNavigator_SetWorldPosition` at `0x01E75F50`. These slot-32
  methods copy absolute world position and must not be treated as movement
  intent or locomotion entry points.

Both are x86 `thiscall` methods taking a pointer to three floats and returning
with `retn 4`. A typed one-slot hook now calls the Hero method unchanged and,
while an appearance proxy is active, copies a finite vector into the NPC method.
Automated run `20260807-235632-761-44712` observed 15 forwarded vectors and
3.209 units of horizontal movement from the native guard. The IDA database names
and vtable comments preserve this mapping.

This seam supports the intended ownership split: local Hero input and future
server movement intent are sources, while each visible NPC's native navigator is
the locomotion consumer. It does not yet prove server correction, action/attack
routing, collision, or safe actor creation/despawn.

## Native creature lifecycle seam recovered on 2026-08-08

The generic creature factory and lifecycle are now separated:

- `CThingCreature_Construct` at preferred `0x01F37E90` (RVA `0x01B37E90`)
  installs generic creature vtable `0x02F1AFE4`;
- `CThingCreature_CreateFromDefinition` at `0x01F38400` is the explicit factory
  used by `TurnCreatureInto`'s type-1 branch. Its only static caller is that
  conversion path, so it is a useful explicit server-spawn candidate but not the
  universal population hook;
- `CThing_RequestDestroy` at `0x01F2E530` (RVA `0x01B2E530`) is the retirement
  request already intercepted narrowly for retained appearance proxies.

Automated run `20260808-000928-546-25080` observed 22 generic-creature
constructor calls before our proxy was requested. All came from worker thread
`41404`, while the Fable game thread was `14576`. A later generic-creature
retirement with `immediate=true` also arrived on `41404`. The constructor hook is
therefore deliberately limited to atomic counts/thread IDs; the structured
event is emitted later from the verified game-thread automation boundary.

This proves that normal regional population passes the constructor seam and
that creation/destruction are not game-thread-only. It does not yet identify the
definition at constructor entry or authorize suppression. The next observer must
correlate constructor pointer -> completed definition/components -> region
registration -> retirement, using a bounded lock-free queue drained outside the
worker hook. Only after that trace is stable should a server allow/deny policy
intercept network-managed NPC population.

## Selective NPC puppet AI seam recovered on 2026-08-08

The apparent visual proxies were complete `CThingAICreature` instances. Hiding
and de-colliding a proxy did not retire its decision engine, so cached hostile
forms could continue targeting and attacking ordinary regional actors while
invisible.

The recovered native ownership chain is:

- `CThingAICreature + 0x1E0` holds its owned `CAIBrain` pointer;
- the observed proxy brain uses preferred vtable `0x02EAF388`;
- vtable slot 4 points to `CAIBrain_Update` at preferred `0x01AD7700`
  (RVA `0x016D7700`) and resumes the brain's fiber-driven decision work;
- `CTCScriptedControl` is a separate creature component, runtime type `0x1F`,
  with preferred vtable `0x02F12704`;
- the existing `SetBound`/`SetFree` script APIs manipulate component type
  `0x31`, not `CTCScriptedControl`, so they are not used as an AI toggle.

The earlier appearance-proxy experiment installed one validated `CAIBrain`
vtable hook and returned early only for exact proxy brain pointers retained in
a bounded registry. The multiplayer implementation now resolves the exact
owning Thing from `CAIBrain +0x20` and applies the current map/action publisher
lease instead. It also freezes active-action updates and rejects new action
submissions on non-owners, preserving the native physics, animation, body, and
replay surface without allowing independent target selection or damage.

Automated run `20260808-105155-995-46800` observed and suppressed 58 guard, 45
villager, 41 hobbe, and 45 reused-guard updates. The hidden cached guard accrued
90 additional suppressed updates before reuse, direct evidence that Drawable
and collision flags alone were insufficient. All four activations reported zero
active and queued scripted actions at registration, native movement still
passed, and the process completed without a fault.

## General AngelScript surface recovered on 2026-08-10

The active appearance path no longer uses the cached-proxy brain hook or a
copied world-position seam described in the historical sections above. It
creates a fresh native creature, gives highest-priority scripted control an
empty action queue, routes verified Hero frame displacement into the creature's
own physics navigator, owns movement-facing after the retail frame update, and
shadows the hidden Hero body to the proxy. Adult-town visual acceptance confirms
native gait and player-owned facing across the supported forms.

The current `CScriptThing` vtable at preferred `0x02E5CBF4` now has exact
current-build validation for:

- name, definition, and data string wrappers at `0x0175B8C4`, `0x0175B8D6`,
  and `0x0175B903`;
- data-string mutation at `0x0175B930`;
- current and home map names at `0x0175C30F` and `0x0175C33C`;
- usability, open-door, summoned, awareness, activation, attachment, and script
  counter wrappers in the `0x0175C398`-`0x0175C74F` range.

The metadata string object is a ref-counted `CCharString`: the outer handle owns
an inner allocation whose UTF-8 buffer pointer is at `+0x4`. Owned return values
are destroyed through `CCharString_Destruct`; the direct name value is borrowed.
Run `20260810-121428-259-19448` read exact definitions, the stable
`SCRIPT_NAME_FABLE_TOGETHER_PUPPET` name, and `BowerstonePosh` current/home maps
for guard, villager, and hobbe before passing movement and restoration.

The framework now also has non-native infrastructure for named events,
cancellable one-shot/repeating callbacks, and typed per-module persistence.
Function handles are released before F5 module discard. Storage is scoped by a
stable relative script-path hash and exposes no arbitrary filesystem path.

A quest-state parity mapping proved that FSE's historic vtable indices are not
direct ABI authority for this executable. Two current-build insertions move the
four quest predicates to slots 299-302: `IsQuestActive` at preferred
`0x01C91E50`, `IsQuestRegistered` at `0x01C91E60`, `IsQuestCompleted` at
`0x01C91E70`, and `IsQuestFailed` at `0x01C91E80`. Run
`20260810-121428-259-19448` safely distinguished registered, active, and
completed fixture quests plus a nonexistent control. Adjacent quest mutations
remain unavailable until their semantics and disposable-save effects are
verified independently.

## Native player ATTACK-to-NPC ability seam recovered on 2026-08-11

The player combat path no longer depends on polling the mouse or on
`GetHeroTargetedThing`. Current-build analysis and runtime observation establish
this chain:

1. Default combat mapping resolves GameAction `ATTACK` (`9`) and creates player
   command kind `0x16` containing `CREATURE_ABILITY_ATTACK`.
2. `CGamePlayerInterface::PollCommand` at preferred `0x01C85870` returns the
   40-byte command. The consumer call at preferred `0x01CCA8D4` was confirmed by
   runtime return RVA `0x018CA8D7`.
3. `CGamePlayer_ProcessAttackAbilityCommand` at preferred `0x01FAECD0` parses
   the native ability ID and charge, resolves the authoritative Hero creature,
   and calls `CThingCreature_SubmitAbility` at preferred `0x01F414A0`.
4. The call returns to preferred `0x01FAEDA8`. That exact return address is the
   fail-closed discriminator used by the proxy hook; unrelated creature
   abilities pass through unchanged.
5. If the source creature is the retained Hero and a proxy is bound, only the
   source pointer is replaced. The original `CThingCreature_SubmitAbility`
   continues, preserving the NPC's native action construction, animation,
   weapon sweep, hit detection, and targeting stack.

Automation run `20260811-143557-839-33304` observed command `0x16`, ability ID
`1101`, source Hero `88ABC800`, routed guard `88446E00`, and
`proxy_routed=true`, then completed all locomotion/facing/shadow/restore gates
and clean shutdown. This proves the native submission handoff; damage
attribution, weapon equipment, live-target hit results, and the other combat
actions remain separate acceptance gates.

## Durable NPC village membership recovered on 2026-08-15

`CTCVillageMember` is component `0x23`. Its serialized `VillageUID` is stored at
`+0x18/+0x1C`; its cached intelligent pointer at `+0x10` resolves the Village
Thing and component `0x22`. The retail idle scheduler component `0xD3` resets
when an AI brain is created, so its queues are transient high-sim state rather
than a durable off-map schedule cursor.

The multiplayer lifecycle now carries one optional `VillageUID` per canonical
NPC. Existing host records cannot be overwritten by a successor map owner's
stale local save. Before high-sim is unfrozen, the materializer calls the
validated retail setter at preferred `0x01F11730`, writes the authoritative UID,
then invokes the reconciliation method at `0x01F11C80`. This rebinds the cached
Village Thing and updates the village's native member collections without
replicating unbounded AI history.

The same setter is now intercepted for runtime changes. Save hydration is
ignored until the Thing is live; an explicit change from the current map owner
is submitted as a generation/epoch-fenced lifecycle mutation. The host updates
its one current record and broadcasts a normal authoritative upsert, while
authoritative local application suppresses hook echo.

## First practical milestones

The lowest-risk multiplayer slice is a two-player same-map session where the
host and guest see interpolated remote avatars moving, facing, and playing
locomotion animations, with stable overhead names. It excludes combat,
inventory, quests, doors, world transitions, and saving.

The first RP-world slice should then use one small region with one
server-authoritative NPC, one custom conversation, one merchant with server-owned
stock, one allowlisted door, and one server daily quest that rewards a test
currency. This exercises the intended authority model without first solving the
entire Albion quest graph.

## Principal risks and gates

- Global/singleton assumptions around `HeroPawn`, camera, controller, quest state,
  and save ownership.
- Remote actors accidentally entering AI, collision, damage, quest, or save
  systems as local actors.
- Quest scripts overwriting server-owned doors, NPC availability, populations,
  or scenery.
- Level streaming and internal travel invalidating cached UObjects.
- Treating dormant UE3 replication classes as proof that Fable gameplay classes
  have correct replication declarations.
- Treating shipped loose developer files as proof that edited files are loaded by
  the Anniversary runtime.
- Local rewards, merchant prices, hit results, or quest counters being trusted by
  the server.
- Save corruption during experiments. Use disposable saves, disable mod-world
  writes to vanilla progression, and make every persistent mutation idempotent.
- Address drift between executable builds. Hash gate, signature scan, validate
  semantic neighbors, and fail closed before installing hooks.
