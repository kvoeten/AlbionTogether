# FableTogether

Experimental multiplayer mod for Fable Anniversary.

![Two players exploring Albion together](media/MovementReplication.gif)

## Current alpha

Two players can load their own Heroes, explore Albion independently, and meet up anywhere. NPCs remain synchronized while players move between regions or split up.

Combat state and health now synchronize, but combat still looks janky because remote player and enemy attack animations are not yet reproduced correctly.

### Done

- Load your own save character and see each other.
- Walk around Albion independently and meet in any region.
- Hero appearance, movement, rotation, and locomotion.
- NPC presence, position, movement, and health.
- NPC ownership when players split across different regions.
- NPCs moving between regions without losing their identity or health.
- Player health synchronization during combat.
- Stable map transitions and background rendering.

### Next

- Remote player and enemy combat animations.
- Hero weapons, damage, projectiles, abilities, death, and revival.
- Shared quests, cutscenes, warps, and world progress.
- Equipment, loot, trading, currency, shops, and property ownership.
- Player names, interaction menus, door/key permissions, and RP tools.
- Proximity voice chat.
- A friendly host/join launcher UI and production networking.

This is an early development release. Back up ordinary saves before testing.

## Install and play

FableTogether currently targets the 32-bit Steam build of Fable Anniversary.

1. Download and extract the release into Fable Anniversary's `Binaries\Win32` folder.
2. Allow the chosen UDP port through the host's firewall. The default is `38171`.
3. Start the host:

   ```powershell
   .\FableTogether.Launcher.exe --host --player-id Host
   ```

4. On the other computer, join using the host's IPv4 address:

   ```powershell
   .\FableTogether.Launcher.exe --join 192.168.1.10 --player-id Guest
   ```

5. Each player selects the save containing the Hero they want to use.

Use `--port <port>` on both computers to choose a different UDP port.

## Build

Build `FableTogether.sln` as `Release | Win32`. Deployable files are written to `bin\Release`.

The project also includes an embedded AngelScript runtime and native scripting APIs for future gameplay mods.
