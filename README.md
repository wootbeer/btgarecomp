# BattleTanx: Global Assault Recompiled  

https://www.youtube.com/watch?v=M_FX5GtOARQ  

This is an extreme beta build.  

Don't worry changes and improvements to come as needed.  
Is very bug-free thus far from what I can see.  
But I am in it for the long haul unlike the ones you are worried about.  
If you find any issues let me know, try to take screenshots, and I will fix.  
Any issues will most likely be with rendering, but I've stomped most of them out.  

You will see this space change a lot in the coming days and everything will be cleaned up.  
I just wanted to get you guys something to play over the weekend, have fun.  

Other platforms and Android to come.  
Stay tuned for updates in the coming days over the next week or so.  

A native PC port of BattleTanx: Global Assault, made by statically
recompiling the N64 game with [N64Recomp](https://github.com/N64Recomp/N64Recomp)
and running it on [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
with [RT64](https://github.com/rt64/rt64) for rendering.

Unofficial, and not affiliated with the rights holders. **No game data is
included.** You need your own dump of the game to build or play this.

## Status

## Getting started

You need your own data files BattleTanx - Global Assault (USA).n64  
SHA-1: 08A9037488C47D1E26CE6F709955639E9F0F0BB8  
Other ones might work but this is the one I used.  

Add portable.txt to make portable.  

## Building

See [BUILDING.md](BUILDING.md).

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

The project's own code is GPL-3.0; see [COPYING](COPYING). The executable
contains the game's code, recompiled from a dump, and the game itself remains
the property of its rights holders.
