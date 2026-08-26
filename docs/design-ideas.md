# AlbionTogether design ideas

This is a living product-design document. It records the intended RP experience,
possible rules, and promising implementation routes. Reverse-engineering evidence
and confidence levels live in [initial-hook-analysis.md](initial-hook-analysis.md);
an idea in this file is not automatically a proven engine capability.

## Design pillars

1. **Albion is a shared physical place.** Players walk roads, open doors, take
   boats, and speak to people. Menus do not replace travel.
2. **The server owns consequential truth.** NPCs, health, inventories, currency,
   property, doors, keys, quests, sentences, and role permissions are not decided
   by a client.
3. **Keep Fable visibly Fable.** Preserve its NPCs, character morphs, clothing,
   hair, tattoos, shops, regions, prison, and delightfully strange social systems.
4. **RP roles create gameplay.** Merchant, guard, warden, prisoner, property
   owner, quest giver, and similar roles should be played by people when useful,
   with NPC coverage when nobody is on duty.
5. **Appearance tells history.** Progression, age, equipment, hair, scars, and
   chosen character identity must be visible to other players.

## The default RP world

- Vanilla story quests are inactive and hidden from players.
- The world starts in a deliberately authored post-story-like state with every
  intended region reachable.
- “Open world” does not mean arbitrary teleporting. A player must use a nearby
  road exit, door, boat, teleporter, or NPC interaction that the server validates.
- Background scripts required for population, scenery, region loading, and basic
  simulation remain enabled where needed.
- The server publishes a versioned world profile for NPC availability, barriers,
  physical travel links, door state, and suppressed story triggers.
- Players normally see only mod-authored activities and quests.

## Server authority

The first practical server can be a host game process plus a separate session
service. A true dedicated/headless simulation is a later goal.

The server owns:

- player account, character, and entity identities;
- player health and authoritative progression;
- NPC existence, region, AI decisions, transform, action, appearance, and health;
- combat results, death/custody decisions, and drops;
- inventories, merchant stock, prices, currency, and trades;
- property ownership, door state, lock policy, and key ownership;
- conversations, quests, progress, rewards, expiry, and repeat policy;
- guard/warden permissions, arrests, sentences, and audit history.

Clients render Fable objects, collect input, and request actions. All Fable object
reads and mutations occur on the game thread.

### Server entity and region lifecycle

Native Fable pointers are temporary bindings, never network identities. A
networked actor record needs at least:

```text
server_entity_id, entity_kind, archetype_definition
region_id, region_instance_id, lifecycle_revision
transform, velocity, movement_intent, action
appearance_profile_id, health_revision, available
```

Each client maintains a region-generation-scoped binding from
`server_entity_id` to the native creature and its required components. When a
region becomes playable, the client reconciles the server's desired set with
the local bindings: create missing actors, update retained actors, and retire
actors absent from the new revision. A map unload invalidates the whole native
generation even if an address is later reused.

Fable's default population path must be intercepted narrowly. Network-managed
NPC definitions are admitted only when present in the authoritative regional
set, while scenery actors, doors, travel machinery, and required background
scripts continue through their normal lifecycle. Different region instances can
therefore feature different server-authored NPC populations without inheriting
every vanilla resident.

The current generic-creature path is a proven creation candidate and its cached
proxy is a useful prototype, but the exact creation/despawn interception and
clean region teardown contract are not yet proven. Those hooks require a normal
creation/registration/removal trace before they become authority boundaries.

The first constructor observation materially narrows this work. Loading the
adult Bowerstone North fixture constructed 22 generic creatures before our proxy,
all on a population worker thread rather than the game thread; a later immediate
retirement also arrived on that worker. Constructor/destructor hooks must
therefore do lock-free capture only. They may never wait for the server, write
diagnostic files synchronously, or mutate the world. A safe simulation boundary
drains those records, assigns or rejects server bindings, and applies the next
region reconciliation revision.

## Player identity and visible appearance

Every replicated player should have an `AppearanceProfile` independent of their
combat snapshot:

```text
appearance_revision
base_creature_or_body
appearance_seed
physique_visual
skill_visual
morality_visual
fatness_visual
age_years and age_visual_stage
hair_definition
beard_definition
tattoo_definitions
clothing_and_armour_definitions
weapon_and_attachment_definitions
additional_morph_or_texture_modifiers
```

The server does not need to replicate every hidden Fable stat. It does need to
replicate the visual inputs produced by those stats. Health remains authoritative
gameplay state; physique, weight, alignment, and similar values may be either
gameplay-backed or visual-only depending on the server ruleset.

The first working arbitrary-creature experiment uses an identity-preserving
proxy: the local `CThingPlayerCreature` remains the object returned to player,
quest, camera, and controller systems, while a non-damageable generic creature
is visible and owns collision after activation. Proxy definitions are cached and
reused rather than destroyed during play. Hero movement vectors can now be
forwarded into the creature's native
`CTCPhysicsNavigator`, which produces real NPC displacement instead of choppy
transform following. This is a useful fallback for skeletons that cannot fit
the Hero presentation stack, but attacks and other semantic actions still need
explicit routing.

Equipment, clothing, hair, beard, tattoos, and weapon attachments are discrete
definition IDs and should update immediately when changed. Morph inputs should be
versioned and reapplied after region loads or proxy recreation.

### Playing as an NPC

Players should be able to adopt approved NPC appearances for jobs, events, or
long-lived character concepts.

The current direction keeps `HeroPawn` hidden as the compatibility object for
camera, save, quest, ability, and hero-only assumptions, but lets the visible
native creature own its locomotion stack. Local Hero input is translated into
the creature's navigator; the Hero then shadows that creature at a safe
simulation boundary. For a remote player or server NPC, server movement intent
and corrections feed the same navigator without creating a local Hero.

This separation also permits routing physical attacks through the visible NPC
while retaining selected Hero-only ability requests. Collision, damage, weapon
compatibility, and each allowed creature skeleton remain explicit profile
capabilities rather than assumptions inherited from appearance alone.

The shipped scripts contain `TurnPlayerInto(...)`, which makes a real creature
swap worth testing. Even if it works, each candidate creature needs validation
for skeleton, locomotion, weapons, interactions, camera, collision, and hero-only
casts before it becomes an allowed player form.

## Aging, greying, and retirement

A server calendar can age a character on a regular cadence—for example, one
Fable year per real week. The cadence and whether offline time counts should be a
server setting.

Suggested lifecycle:

- young adult: normal starting profile;
- mature: subtle facial/skin and hair changes;
- old: strong age morph, grey hair/beard variants, and wrinkles;
- venerable: maximum visual age and `retirement_eligible` status;
- retired: the character becomes a persistent NPC, property heir/owner, mentor,
  shopkeeper, or historical figure while the player starts a successor.

Retirement should be an RP event, not surprise character deletion. Servers can
make it mandatory, optional, or admin/story-triggered. A successor/inheritance
system could transfer selected property, keys, family wealth, or reputation while
leaving room for an estate dispute—because Albion.

The engine clearly has hero morph and hair-modifier systems, but the exact
adult-to-old control and wrinkle/grey channels still need runtime discovery. If
Fable bundles these into an age morph, use it. Otherwise the mod can select old
texture/hair modifiers explicitly while retaining the canonical server age.

## Progression, XP, stats, and abilities

Progression should be server-owned and deliberately slower than vanilla. The
server publishes the player's XP pools, trainable levels, purchased upgrades, and
ability grants. The native experience menu can be reused as presentation only if
its purchase action is redirected to the server.

The desired rules can support both ordinary XP upgrades and activity-gated
abilities:

- stat ranks have configurable XP curves, prerequisites, and caps;
- later ranks may require training, an NPC teacher, a faction, a quest, an item,
  a location, or a server event in addition to XP;
- spells are explicit grants, not automatically available because the player has
  enough Will XP;
- particular spell levels can require separate unlocks;
- refunds and respecs are server transactions with an audit trail;
- the server replicates visual morph results even when it does not expose every
  underlying Fable stat to peers.

The initial progression record can be:

```text
xp_general, xp_strength, xp_skill, xp_will
stat_ranks by stable stat ID
ability_grants by stable ability ID and maximum rank
equipped_abilities
progression_revision
```

### Slow Time

Slow Time is banned rather than merely expensive. Defense in depth:

1. Exclude it from server ability grants and all trainers/rewards.
2. Remove or disable it in the experience-spending UI.
3. Strip it from loaded vanilla saves and equipped/cycled ability lists.
4. Reject its equip and cast action locally.
5. Reject the action on the server and never replicate a time-scale effect.

This matters because a client-only menu change can be bypassed by save editing or
calling the underlying ability directly. The stable native enum identifies it as
`HERO_ABILITY_TIME_SPELL`, and the data contains a dedicated
`HERO_ABILITY_TIME_SPELL_INVENTORY_THING`.

## NPCs and custom conversations

- The host runs real NPC AI; guests render non-AI proxies.
- The server decides whether an NPC exists, where it is, its current action, and
  whether it is available to interact.
- Custom conversations are server-owned dialogue trees.
- Existing localization/speech IDs can be reused, but new unvoiced text and
  choices should work through a custom GFx conversation page.
- Choices can open a store, start or progress a quest, unlock travel, exchange an
  item, change a role, or request a guarded door action.
- An interaction lock/revision prevents two players from completing an exclusive
  NPC transaction simultaneously.

## Stores, trading, and a real economy

- NPC merchants have server-owned stock, buy/sell rules, currency, restock time,
  and availability.
- Existing Fable trade pages are presentation; the server issues quotes and
  atomically commits inventory/currency changes.
- Player-to-player trade uses two-party acceptance and escrow.
- Property, scarce materials, equipment, services, and keys can participate in
  the same economy.
- Prices should support fixed, regional, stock-sensitive, event-driven, and
  player-run policies.

## Custom and daily quests

The server can publish daily/weekly quests for currency, materials, reputation,
licenses, spell access, or role progression.

Good first objective types are easy for the authoritative server to observe:

- kill a particular creature definition;
- deliver materials to a named NPC or merchant;
- visit a region through a valid physical travel link;
- perform a trade or craft/service interaction;
- speak to or assist a particular NPC;
- complete a guard patrol or prisoner transfer;
- participate in a scheduled RP event.

Fable's native quest log/cards are desirable presentation, but the server owns
acceptance, counters, expiry, completion, and rewards. A generic bridge quest is
preferable to registering a fresh native quest name every day. Custom GFx is the
fallback for arbitrary dynamic text.

Daily reward claims are idempotent and keyed by player, quest version, and claim
ID. A reconnect or repeated client packet cannot pay twice.

## Guard shifts, crime, prison, and wardens

### Taking a guard shift

- Only approved players can start a guard shift.
- Starting a shift applies a server role, jurisdiction, duty record, guard
  nameplate, approved guard creature/outfit appearance, and issued equipment.
- Duty equipment and keys are tracked separately from personal inventory where
  the server wants guaranteed return; other servers may intentionally allow
  corruption, loss, and black-market trade.
- Ending a shift restores the player's normal appearance and permissions.
- Guard powers are server permissions, never inferred only from wearing a guard
  outfit.

The game has Bowerstone, Knothole Glade, prison, coloured, crossbow, and other
guard creature definitions. Start with one human-compatible guard visual profile
and expand only after animation/equipment validation.

### Arrest and custody

The dramatic version is: an on-duty guard defeats a wanted player, the lethal
result is converted into custody, and the prisoner is moved to jail.

The server should ideally treat this as `defeated -> arrested`, not an ordinary
death followed by an unrelated teleport:

1. Validate that the attacker is an on-duty guard with jurisdiction and that the
   target is arrestable/wanted under server policy.
2. Intercept the authoritative lethal result before normal death rewards, drops,
   resurrection, or save mutation.
3. Apply a restrained/custody state and clear combat safely.
4. Transfer the prisoner through a jail intake link to an assigned cell.
5. Create a sentence record with issuer, cause, start, duration, release policy,
   cell, and appeal/admin fields.
6. Publish the prisoner's restricted travel and door permissions.

Servers can also support voluntary surrender, nonlethal restraint, fines,
bounties, trials, and prisoner transport for richer RP.

Fable contains both Bowerstone Jail and a large prison complex with prison cells,
office, barracks, courtyard, torture chamber, paths, secret passages, a cell key,
guard/prisoner factions, a prison warden, and existing prison quests. That gives
the system multiple useful venues rather than a single teleport room.

### Warden and prison RP

- An approved warden can assign cells, issue/revoke keys, set sentences, grant
  visits, authorize release, and delegate guard permissions.
- Cell doors stay server-authoritative even when their local scripts want a
  different state.
- Prisoners can retain proximity voice and selected inventory according to server
  policy.
- Work details, contraband, visits, trials, bribery, escape attempts, and secret
  tunnels can become server quests/events.
- A timed sentence should survive disconnects. Whether time passes offline is a
  server policy.

### Physical, tradable keys

Keys should be non-fungible server item instances, even if Fable renders them as
ordinary inventory objects:

```text
key_instance_id
key_template_id
current_owner_or_container
opens_lock_set
issuer and issued_at
expires_at or revoked_at
copy_generation
metadata and revision
```

A door can grant access through any combination of:

- possession of a compatible key instance;
- guard/warden duty role;
- explicit player or group allowlist;
- property ownership/tenancy;
- temporary conversation/quest authorization;
- admin/emergency override.

Because ownership is server-side, a key can be handed over, stolen, pickpocketed,
hidden in a chest, sold through a secret trade, copied if the server permits, or
revoked. The server atomically transfers the key instance; a duplicated local
inventory object has no authority.

Fable already contains named objects such as `OBJECT_PRISON_CELL_KEY`,
`OBJECT_GRAVEYARD_GATE_KEY`, `OBJECT_HOBBE_CAVE_DOOR_QUEST_KEY_01`, other quest
keys, a prison key rack, locked-door dialogue, and picklocking. We should reuse
their models/interactions where convenient but keep the lock-to-key relation in
the server so it is general, auditable, and tradable.

## Property and doors

- Persistent properties and doors have stable server IDs.
- Property ownership can grant tenancy, rent, storage, store operation, and door
  administration.
- Each door has a versioned open/closed/locked state and access policy.
- The client asks to interact; the server checks distance, region, state revision,
  roles, allowlists, and key ownership before broadcasting the result.
- A local quest/script attempt to change an RP-managed door is reconciled back to
  server state.

## Combat and health

- The server owns enemy/player health, hit validation, damage, death/custody,
  drops, and revisions.
- Clients can predict effects and animation but cannot award damage.
- NPC AI runs once on the authoritative map or per-entity action lease owner;
  the host remains the durable state and conflict-resolution authority.
- Player and remote visual proxies keep their native NPC body, navigator,
  animation, and scripted-action stack but have autonomous `CAIBrain` updates
  paused for their entire active or cached lifetime. Local input or a server
  command must explicitly submit locomotion, targeting, and attacks; hiding a
  proxy is never treated as behavioral retirement.
- Guard custody is a distinct terminal outcome from ordinary PvP death.
- Remote proxies must not take local damage and then receive server damage again.

## Proximity voice

- Voice uses a separate Opus media channel keyed to mod entity IDs.
- Same-region replicated positions drive distance falloff and stereo pan.
- Speaking indicators use the same overhead-name UI.
- Mute, block, moderator action, recording policy, and abuse reports are first-class
  features.
- Later, door/room state can drive occlusion so prison cells, houses, and taverns
  sound spatially believable.

## Candidate vertical slices

1. Two players in one region with transform/action interpolation, appearance, and
   overhead names.
2. One authoritative NPC with availability, a custom conversation, and a
   server-owned store.
3. One daily quest with a material/currency reward and idempotent claim.
4. One property and door with an allowlist plus one tradable server key.
5. One approved guard shift, guard visual profile, voluntary surrender, prison
   intake, cell assignment, and timed release.
6. Authoritative combat/custody, prison breakout activities, and warden tooling.
7. Server progression, activity-gated abilities, Slow Time removal, and replicated
   stat/age appearance.
8. Proximity voice with door-aware occlusion.

## Open design decisions

- Does one real week always equal one character year, and does offline time count?
- Is retirement mandatory, opt-in, or a server-configurable RP rule?
- Do guards lose issued gear/keys on death, and may corruption/black-market sales
  be part of the intended game?
- Does a prisoner serve time while offline?
- Which abilities are ordinary XP purchases, which require special activities,
  and which are banned?
- Are stat ranks authoritative combat values, visual identity only, or both?
- Can keys be copied, pickpocketed, destroyed, or recovered from lost-and-found?
- Which prison is the default, and can different towns operate separate jails?
- How much of Fable's native crime/fine system should remain visible versus being
  replaced by server law?
