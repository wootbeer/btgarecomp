# BattleTanx: Global Assault (1999 N64) Recompiled and Port  
=====================

https://www.youtube.com/watch?v=M_FX5GtOARQ  

You will need the game file from a licensed copy in order to play it.  

A native PC port of BattleTanx: Global Assault, made by statically
recompiling the N64 game with [N64Recomp](https://github.com/N64Recomp/N64Recomp)
and running it on [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
with [RT64](https://github.com/rt64/rt64) for rendering.  

Unofficial, and not affiliated with the rights holders. **No game data is
included.** You need your own dump of the game to build or play this.  

Android build is planned, more to come stay tuned.  
  
## Game Files  
You need your own data files BattleTanx - Global Assault (USA).n64  
SHA-1: 
08A9037488C47D1E26CE6F709955639E9F0F0BB8  
Place in an accessible folder onto your device.  
  
Add a blank portable.txt in the application's folder to make portable if you want to save settings locally.  
  
## Building  
See [BUILDING.md](BUILDING.md).
  
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
