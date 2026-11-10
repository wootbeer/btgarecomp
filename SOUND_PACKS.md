# Making a sound pack

A sound pack replaces the game's sound effects with your own. It works like a texture pack: drop it in the mods folder and turn it on in the Mods tab.

## What goes in a pack

A pack is a zip file:

```
my_pack.zip
├── btga_audio.json
└── sfx/
    ├── BANK_00_INSTR_0008.wav
    ├── BANK_00_INSTR_000F.ogg
    └── ...
```

- `btga_audio.json` marks the zip as a sound pack. It can just contain `{}`.
- `sfx/` holds your sounds. Each file is named after the effect it replaces.
- Files can be `.wav`, `.ogg` or `.mp3`. Mono or stereo, any sample rate.
- Leave out any effect you don't want to change. It keeps the game's sound.

## Effect names

The names are the ones N64SoundListTool gives the game's effects when you rip them: `BANK_00_INSTR_0000` up to `BANK_00_INSTR_004B`. The easiest way to find a sound is to rip them all with N64SoundListTool and listen.

## Speed and pitch

N64SoundListTool saves every effect at 22050 Hz. That's the reference:

- A file at 22050 Hz plays at the same speed as the original effect.
- So if you edit a rip and save it at 22050 Hz, it plays just like the original did.
- For a new sound, make it sound right next to the rips in a media player. Then it'll sound right in game too. Any sample rate is fine, the game converts it.

Most effects play slower and lower in game than the rip sounds in a media player. That's normal. Your sound gets the same treatment.

## Loops

Only effects that already loop in the game can loop, like the tank treads.

For those, the loop comes from (first one found):
1. `loop_start` and `loop_end` in `btga_audio.json`
2. the WAV's loop points (most audio editors can set these)
3. `LOOPSTART` and `LOOPEND` (or `LOOPLENGTH`) tags in an Ogg file
4. otherwise the whole file loops

Loop points in a sound that doesn't loop in the game are ignored.

## Options in btga_audio.json

All optional:

```json
{
  "name": "My Pack",
  "author": "Me",
  "sfx": {
    "BANK_00_INSTR_000F": { "semitones": -2, "volume": 0.8 },
    "BANK_00_INSTR_0019": { "loop_start": 1200, "loop_end": 48000 }
  }
}
```

- `semitones`: pitch up (positive) or down (negative). It changes the speed too, like a record. Fractions are fine, like `1.5`.
- `volume`: `1` is unchanged, `0.5` is half.
- `loop_start`, `loop_end`: the loop, counted in samples of your file.

## Installing

1. In the game, open the Mods tab and press Open Mods Folder.
2. Put the zip in there.
3. Turn the pack on in the Mods tab.
4. Restart the game. Packs load when the game starts.

If two packs replace the same effect, the one higher in the list wins.

## Using a folder instead of a zip

While you're working on a pack, it's easier to use a plain folder: change a file, restart, listen. A folder needs one extra file, `mod.json`, next to `btga_audio.json`:

```json
{
  "game_id": "btga",
  "id": "my_pack",
  "version": "1.0.0",
  "authors": ["Me"],
  "minimum_recomp_version": "0.0.0",
  "display_name": "My Pack"
}
```

`id` has to be different from every other mod you have.

## BattleTanx 1 sounds

To use the first game's versions of the shared effects, use the BattleTanx 1 Sound Effects option on the Sound tab instead. It already matches each sound's pitch. If a pack and that option both replace an effect, the pack wins.

## If a sound doesn't change

- Check the file name: `BANK_00_INSTR_` plus four characters, inside the `sfx` folder.
- Check the pack is turned on, and restart the game.
- Use a plain WAV (16-bit) to rule out a file the game can't read.
