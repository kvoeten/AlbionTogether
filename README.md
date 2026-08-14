# FableTogether

Experimental multiplayer mod and script-extender framework for Fable Anniversary.

![Two players moving together in Fable Anniversary](media/MovementReplication.gif)

## Alpha status

The first alpha supports two players loading their own save characters, connecting to a host, and exploring Fable's world together. Remote Heroes reproduce the selected character's appearance and use native Fable locomotion while replicated movement and yaw are smoothed between network updates.

This is an early development release. Back up ordinary saves before testing and expect incomplete gameplay systems.

### Implemented

- Headless Win32 launcher and client DLL injection.
- Command-line UDP hosting and joining with stable player identities.
- Independent save selection for the host and joining player.
- Actor-channel replication structured for more than one remote player.
- Same-map remote Hero creation and removal based on player map membership.
- Position and yaw replication with interpolation, short extrapolation, and native locomotion animation.
- Selected-save Hero appearance replication, including clothing, morph values, appearance modifiers, and skeletal bone scaling.
- Safe map-transition teardown and destination-world remote Hero recreation.
- Background movement and rendering for unfocused local development instances.
- Host-routed map authority and multi-peer transport foundations.
- Embedded AngelScript runtime with typed, validated native game APIs.
- Structured runtime logs, event traces, and automated multiplayer acceptance scenarios.

### Still to do

- User-facing host/join launcher UI, session discovery, invitations, NAT traversal, and production networking security.
- Host-owned quest, cutscene, warp, and persistent world-state synchronization.
- Authoritative combat, health, damage, projectiles, abilities, death, and revival.
- Dynamic equipment, inventory, loot, trading, currency, and property ownership synchronization.
- Server-authoritative NPC spawning, cross-map NPC lifecycle, AI state, stores, and conversations.
- Custom quests, progression rates, spell restrictions, and server-driven activities.
- Player names, custom HUD elements, interaction menus, door/key permissions, and roleplay tools.
- Proximity voice chat.
- Broader compatibility testing, reconnect/recovery behavior, configuration UI, and polished distribution.

## Install and play

FableTogether currently targets the 32-bit Steam build of Fable Anniversary.

1. Download and extract the release into Fable Anniversary's `Binaries\Win32` folder so `FableTogether.Launcher.exe` sits beside `Fable Anniversary.exe`.
2. Allow the chosen UDP port through the host's firewall. The default is `38171`.
3. Start the host:

   ```powershell
   .\FableTogether.Launcher.exe --host --player-id Host
   ```

4. Start the other player on a separate machine, using the host's IPv4 address:

   ```powershell
   .\FableTogether.Launcher.exe --join 192.168.1.10 --player-id Guest
   ```

5. Each player selects the save whose Hero they want to use. Remote Heroes appear whenever players occupy the same map.

Use `--port <port>` on both sides to choose a different UDP port.

## Build

Build the Win32 Release solution in Visual Studio or with MSBuild:

```powershell
msbuild FableTogether.sln /p:Configuration=Release /p:Platform=Win32
```

Deployable files are written to `bin\Release`. During development, the launcher also falls back to:

```text
D:\SteamLibrary\steamapps\common\Fable Anniversary
```

## Development tests

```powershell
.\bin\Release\FableTogether.Launcher.exe --multiplayer-test
.\bin\Release\FableTogether.Launcher.exe --multiplayer-roster-test
.\bin\Release\FableTogether.Launcher.exe --multiplayer-transition-test
.\bin\Release\FableTogether.Launcher.exe --multiplayer-playtest
```

These scenarios use isolated fixture saves and write their logs below `bin\Release\artifacts`.

## Documentation

- [Scripting framework](docs/scripting-framework.md)
- [Local multiplayer development](docs/local-dual-instance.md)
- [Automated session roadmap](docs/automated-session-roadmap.md)
- [Native hook analysis](docs/initial-hook-analysis.md)
- [Hero appearance proxy](docs/appearance-proxy.md)
- [Design ideas](docs/design-ideas.md)
