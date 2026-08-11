# FableMenuAnniversary reference map

## Provenance and use

Reference: <https://github.com/ermaccer/FableMenuAnniversary>

Local analysis is pinned at commit
`164d5247f09ed5e03b6a323be4373a3ee66686a7` under
`.analysis/third-party/FableMenuAnniversary`. No license was visible in that
snapshot, so FableTogether uses it as reverse-engineering evidence and does not
copy its menu or implementation wholesale.

The project targets the Steam Anniversary executable and its addresses align
with our disposable native IDA image. They remain candidates until the target
fingerprint, function prefix or vtable, pointer ownership, and runtime behavior
pass FableTogether's validation gates.

## Player acquisition

| Concern | Preferred address/layout | Confidence |
| --- | --- | --- |
| Main game singleton | pointer at `0x0362AE48` | reference candidate |
| Player manager | main game component `+0x24` | reference candidate |
| `CPlayerManager::GetPlayer` | `0x01CFD7D0` | reference candidate |
| `CPlayer::GetCharacterThing` | `0x01EC1600` | corroborates our Hero/thing model |
| Thing component tree | `CThing + 0x44`, sentinel at `+0x48` | corroborated by current hooks |
| Component lookup | `0x01B80030` | corroborated by current component access |

The component lookup uses numeric type IDs. Relevant candidates are:

| Component | ID |
| --- | ---: |
| HeroMorph | `0x03` |
| HeroStats | `0x04` |
| Hero | `0x29` |
| Enemy | `0x49` |
| GraphicAppearance | `0x5B` |
| HeroExperience | `0x68` |
| RegionFollower | `0x7A` |

## Data candidates

- `CThing + 0xCC`: maximum health.
- `CThing + 0xD0`: current health.
- `CTCHeroStats + 0x38`: age.
- `CTCHeroStats + 0x40`: fat level.
- `CTCHeroStats + 0x44`: gold.
- `CTCHeroStats + 0x60/+0x64`: current and maximum will power.
- `CTCHeroMorph`: update flag near `+0x55`, followed by normalized strength,
  berserk, will, skill, age, alignment, fat, and other presentation values. The
  exact post-flag packing must be revalidated before use.
- `CTCHeroExperience + 0x14`: general experience.
- `CTCHeroExperience + 0x18`: pointer to Strength, Skill, and Will experience
  entries.

These fields cover most of the values we need for a server character snapshot:
health, will, currency, progression, age, body morphs, and alignment-driven
appearance. Equipment, hair, tattoos, and clothing still require their own
component and inventory traces.

The reference also identifies `CTCGraphicAppearance` calls for alpha, color,
and scale at `0x01DA20A0`, `0x01DA2180`, and `0x01DA2010`. Alpha is especially
relevant to keeping the authoritative Hero functionally present while hiding
its presentation.

## FableTogether exposure policy

The first public surface should return immutable typed snapshots rather than
raw component pointers:

- `PlayerStatsSnapshot`: health, maximum health, will, maximum will, gold, and
  general/Strength/Skill/Will experience.
- `HeroMorphSnapshot`: age plus normalized strength, skill, will, alignment,
  fat, and berserk presentation channels.
- `HeroPresentationSnapshot`: definition, equipment/hair/tattoo identities
  once those systems are mapped.

Reads can become `Verified` after pointer and range validation plus repeated
adult-save samples. Writes stay `Experimental` until we find the retail setter
or prove a direct write has the required recalculation, save, and appearance
side effects. Multiplayer code should synchronize semantic values through the
server and apply them through these typed services; scripts never receive raw
addresses.
