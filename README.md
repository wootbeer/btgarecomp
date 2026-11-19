# BattleTanx: Global Assault (1999 N64) Recompiled and Port  
  
https://www.youtube.com/watch?v=M_FX5GtOARQ  
  
You will need the game file from a licensed copy in order to play it.  
  
A native PC port of BattleTanx: Global Assault, made by statically
recompiling the N64 game with [N64Recomp](https://github.com/N64Recomp/N64Recomp)
and running it on [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
with [RT64](https://github.com/rt64/rt64) for rendering.  

Unofficial, and not affiliated with the rights holders. **No game data is
included.** You need your own dump of the game to build or play this.  

Early Android build is now posted. Optimized for retro handhelds but will work with touch.
  
## Features  
- **Netplay**: online play for up to 4 players with join codes, public games, chat and each player's own full-screen view  
- **Local multiplayer** for up to 4 players, with controllers or keyboard and mouse  
- **Widescreen**, higher resolutions and anti-aliasing  
- **Higher framerates** (interpolated), plus Lossless Scaling frame generation if you own it  
- **Draw Distance** out to the map's edge, with Fog on or off and Steady Distant Scenery against pop-in  
- **Screen filters** (CRT, Scanlines, LCD) and Bloom  
- **Vulkan or DX12**  
- **Remappable controls**, Mouse Stick, rumble and per-player Controller/Rumble Pak options  
- **Cheats and hacks** menus (Level Select, All Weapons, gangs and more)  
- **Mods, texture packs and sound packs**, BattleTanx 1 Sound Effects, reverb and more sound channels  
- **Add to Steam** from the launcher  
  
## Game Files  
You need your own data files BattleTanx - Global Assault (USA).n64  
SHA-1: 
08A9037488C47D1E26CE6F709955639E9F0F0BB8  
Place in an accessible folder onto your device.  
  
New installs are portable: settings, saves and mods stay in the game's folder (portable.txt). If you've played an earlier version, your settings stay in your user profile; see [COMMAND_LINE.md](COMMAND_LINE.md#portable-mode) to switch.  
  
## Command Line  
Start straight into the game, load a ROM or change any setting from a shortcut or frontend. See [COMMAND_LINE.md](COMMAND_LINE.md).  
  
## Building  
See [BUILDING.md](BUILDING.md).
  
## Sound Packs  
Replace the game's sound effects with your own. See [SOUND_PACKS.md](SOUND_PACKS.md).
  
## Mods  
- [Gang Mod](https://github.com/wootbeer/btgarecomp-gang-mod)  
- [Gun Buddy Mod](https://github.com/wootbeer/btga-gunbuddy-mod)  
- [Mod template](https://github.com/wootbeer/btgarecomp-mod-template), to start your own mod  
  
## Issues and Limitations

Known:  
- Super beta status. Tested on Windows 10.  
  
Else:  
- Please see the “Issues” section in GitHub.  
  
## Credits  
- [N64Recomp and N64ModernRuntime](https://github.com/N64Recomp) by Mr-Wiseguy
  and contributors, which this port is built with
- [RT64](https://github.com/rt64/rt64) by Dario and contributors, the renderer
- [RecompFrontend](https://github.com/N64Recomp/RecompFrontend) by the N64Recomp
  contributors, the launcher and input layer
- [splat](https://github.com/ethteck/splat) and its N64 tooling
  (`spimdisasm`, `rabbitizer`), used for the ROM-splitting work in
  `tools/` and logged in STATUS.md

## License  
The project's own code is GPL-3.0; see [COPYING](COPYING). The game itself remains
the property of its rights holders.  
