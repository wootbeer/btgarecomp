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
  
## Game Files  
You need your own data files BattleTanx - Global Assault (USA).n64  
SHA-1: 
08A9037488C47D1E26CE6F709955639E9F0F0BB8  
Place in an accessible folder onto your device.  
On Android the app asks for it on first start and keeps its own copy. Use Change ROM on the launcher to pick a different one.  
  
PC: add a blank portable.txt in the application's folder to make portable if you want to save settings locally.  
  
## Android  
Download the APK from Releases and install it (allow installs from unknown apps).  
Needs Android 9 or newer, 64-bit, with Vulkan.  
Touch controls show during a match and hide while a gamepad is in use; touch the screen to bring them back. Change their size, opacity, and joystick or D-pad in Settings > Touch Controls.  
  
## Building  
See [BUILDING.md](BUILDING.md). For Android, see [android/README.md](android/README.md).
  
## Issues and Limitations

Known:  
- Super beta status. Tested on Windows 10.  
- Android: tested on Retroid Pocket 6.  
  
Else:  
- Please see the “Issues” section in GitHub.  
  
## Credits  
- [N64Recomp and N64ModernRuntime](https://github.com/N64Recomp) by Mr-Wiseguy
  and contributors, which this port is built with
- [RT64](https://github.com/rt64/rt64) by Dario and contributors, the renderer
- [RecompFrontend](https://github.com/N64Recomp/RecompFrontend) by the N64Recomp
  contributors, the launcher and input layer
- [SDL](https://www.libsdl.org) for the Android app's window and input
- Descore, my Android shell, for the touch controls
- [splat](https://github.com/ethteck/splat) and its N64 tooling
  (`spimdisasm`, `rabbitizer`), used for the ROM-splitting work in
  `tools/` and logged in STATUS.md

## License  
The project's own code is GPL-3.0; see [COPYING](COPYING). The game itself remains
the property of its rights holders.  
