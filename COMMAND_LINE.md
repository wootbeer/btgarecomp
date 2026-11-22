# Command line

BattleTanx: Global Assault Recompiled can be started with options, for
shortcuts, Steam launch options and frontends like ES-DE, Playnite or
LaunchBox.

```
BattleTanxGARecompiled [options] [ROM file]
```

On Linux, use `BattleTanxGARecompiled.sh` from the game's folder.

## Starting

| Option | What it does |
| --- | --- |
| `--rom <file>` | Loads this ROM, as the launcher's Load ROM does. It's checked and kept, so it's only needed once. A file name on its own does the same. |
| `--play` | Starts the game straight away, skipping the launcher (`--start` and `--skip-launcher` work too). Needs a ROM loaded, now or before. |
| `--fullscreen` | Fullscreen. |
| `--windowed` | Windowed. |
| `--netplay` | Opens the Netplay screen once the launcher is up. |
| `--list-settings` | Prints every setting, its values and what it's set to now, then quits. |
| `--version` | Prints the version and quits. |
| `--help` | Prints the options and quits. |

For a frontend, this loads the ROM it passes and goes straight into the game:

```
BattleTanxGARecompiled.exe --play "%ROM%"
```

## Settings

Any option in the menu's General, Graphics, Sound, Cheats and Hacks tabs can
be set by its id:

```
--<setting>=<value>
--<tab>.<setting>=<value>
--set <setting>=<value>
```

For example:

```
BattleTanxGARecompiled.exe --draw_distance=Far --fog=Off --graphics.msaa_option=4x
```

- A choice is given by its name (any case, spaces optional), e.g. `Far` or
  `Very Far`, `4x` or `MSAA4X`, or by its number.
- A number has to be in the option's range; a `%` after it is fine.
- A switch is `on` or `off` (`true`/`false`, `yes`/`no` and `1`/`0` work too).
- The id on its own works when only one tab has it; otherwise put the tab in
  front (`graphics.`, `sound.`, ...).

Settings given this way are saved, the same as changing them in the menu.
The menu's Restart keeps the other options but skips these settings, so
anything you changed in the menu since stays as you set it.

`--list-settings` shows the ids and values. Run it from a terminal (on
Windows, Command Prompt or PowerShell in the game's folder):

```
BattleTanxGARecompiled.exe --list-settings
```

Controls are set up in the Controls tab, and mods in the Mods tab; they don't
have command-line options.

If an option isn't understood, the launcher says so when it opens (and the
game still starts).

## Portable mode

The game keeps its settings, saves, mods and the loaded ROM in its own folder
when a file called `portable.txt` is there. A new install starts that way:
the game creates `portable.txt` the first time it runs.

If you've played an earlier version, your settings are in your user profile
(`%LOCALAPPDATA%\btgarecomp` on Windows, `~/.config/btgarecomp` on Linux), and
the game keeps using them there. To switch either way:

- Portable: put an empty `portable.txt` in the game's folder (copy your files
  over from the profile folder to keep them).
- Profile: delete `portable.txt` from the game's folder.

The game folder has to be one you can write to. In a folder like Program Files
the game uses your profile instead.
