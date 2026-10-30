# Dance Dance Rotation Nexus Addon — Linux

Learn Guild Wars 2 rotations to the beat, with **Nexus on Linux through Wine/Proton**. A native port of the Blish HUD module [Dance Dance Rotation](https://github.com/campbt/DanceDanceRotation) 1.8.1 by Shooper.

Skill rotations play as "songs" in a rhythm game: notes scroll towards a target and you press the matching skill hotkey when they reach it. Every press is graded Perfect, Great, Good or Boo; notes that pass are a Miss. The 102 Snow Crows rotations shipped with 1.8.1 are built in, and you can add more from the [Dance Dance Rotation Composer](https://campbt.github.io/DanceDanceRotationComposer/create.html) or arcdps logs.

Nexus runs inside the Windows game process, so the addon is a **Windows x64 DLL built on Linux**. Blish HUD and .NET are not required. Nexus must already be installed and working in your GW2 prefix.

## Linux distribution compatibility

**The same DLL is intended to work on any x86_64 Linux distribution where GW2 and Nexus already work through Wine/Proton.** It has no Fedora-specific runtime dependency. Game paths and Wine/Proton configuration may differ. Tested on Fedora; other distributions have not been tested individually.

## Install on Linux

The installer copies the DLL, backs up the Nexus configuration and any existing DLL, and enables **automatic loading on the next game launch**. Run project scripts as your regular user.

### Option 1: ready-made DLL (no compilation)

```bash
git clone https://github.com/insightfulrival/Dance-Dance-Rotation-Nexus-Addon-Fedora.git
cd Dance-Dance-Rotation-Nexus-Addon-Fedora
mkdir -p dist
curl -fL --retry 3 -o dist/dance-dance-rotation.dll \
  https://github.com/insightfulrival/Dance-Dance-Rotation-Nexus-Addon-Fedora/releases/latest/download/dance-dance-rotation.dll
curl -fL --retry 3 -o dist/SHA256SUMS \
  https://github.com/insightfulrival/Dance-Dance-Rotation-Nexus-Addon-Fedora/releases/latest/download/SHA256SUMS
(cd dist && sha256sum --check --ignore-missing SHA256SUMS)
python3 install.py
```

Close GW2 first. If the game is running, `python3 install.py --allow-running` only copies the DLL; then load **Dance Dance Rotation (Fedora port)** from the Nexus addon list. Nexus remembers that choice when the game exits.

### Option 2: build and install with one command

Install the prerequisites for your distribution:

```bash
# Fedora / Nobara and other DNF-based systems
sudo dnf install git python3 curl tar xz coreutils binutils
# Ubuntu / Debian / Linux Mint / Pop!_OS
sudo apt update && sudo apt install git python3 curl tar xz-utils coreutils binutils
# Arch Linux / EndeavourOS / CachyOS
sudo pacman -Syu --needed git python curl tar xz coreutils binutils
# openSUSE
sudo zypper install git python3 curl tar xz coreutils binutils
```

```bash
git clone https://github.com/insightfulrival/Dance-Dance-Rotation-Nexus-Addon-Fedora.git
cd Dance-Dance-Rotation-Nexus-Addon-Fedora
./setup-linux.sh
```

The script uses **Zig 0.17.0**. If a matching `zig` or `python-zig` is unavailable, it downloads the official Linux compiler into `.tools/`, verifies its pinned SHA-256, then builds, tests and installs the addon. It does not change the system toolchain.

### Other game locations

The default is `~/.local/share/Steam/steamapps/common/Guild Wars 2`. For another Steam library, Lutris or Bottles, pass the folder containing `Gw2-64.exe`, Nexus's `d3d11.dll` and `addons/Nexus/AddonConfig.json`:

```bash
python3 install.py --game '/path/to/Guild Wars 2'
./setup-linux.sh --game '/path/to/Guild Wars 2'
```

Steam usually needs the launch option `WINEDLLOVERRIDES="d3d11=n,b" %command%` for Nexus. Never rename this addon's DLL to `d3d11.dll`.

## Getting started

1. Start GW2. On the first start the **Welcome** window explains the three steps.
2. **Set hotkeys**: open **Settings** (toolbar button, Nexus Options, or right-click the corner icon). Each Ability Hotkey must match your in-game key. Press **Set**, then the key (Esc cancels). Mouse buttons 3–5 and Ctrl/Alt/Shift combinations work. Defaults are the GW2 defaults: `1`–`0`, `F1`–`F5`, `` ` `` swap, `V` dodge.
3. **Select a song** in the Song List. With *Only show current profession songs*, the list shows songs for your character's profession (from MumbleLink).
4. **Press Play**, or simply press the first skill: with *Start with first skill*, the first note waits on the target and its hotkey starts the song.

Open the notes window with the corner icon or **Ctrl+Alt+D** (change it in Nexus Keybinds).

| Control | Action |
| --- | --- |
| ☰ / ⚙ toolbar icons, song title | Toggle Song List / Song Info |
| ■ / ▶ ❚❚ | Reset / Play / Pause (Play is disabled in No Miss Mode: press the note key) |
| Lock / Unlock | Hide the title bar and fix the window, e.g. over the skill bar |
| Hover a note while paused | Ability name, time in the rotation and cast duration |
| Song List: Add From Clipboard | Import a song's JSON (saved to `addons/dance-dance-rotation/customSongs/`) |
| Song List: Create Song | Open the Composer website |
| Song List: trash icon | Delete a custom song file, or hide a built-in song until the next addon update |
| Song Info | Build link, copy build template, No Miss Mode, Note Speed, Note Pace, Start At, utility remap |
| Nexus Keybinds | Toggle Play/Stop, Play, Pause, Stop, toggle Song List / Song Info (unbound by default) |

Settings (as in the original): notes orientation (right-to-left, left-to-right, top-to-bottom, bottom-to-top, ability bar), background transparency, start with first skill, auto hit Weapon 1, ability icons on notes, hotkey labels, profession filter, compact mode (6/3/1 lanes) and next-abilities strip (0–10). Settings and per-song practice settings are saved in `addons/dance-dance-rotation/settings.txt`. JSON files added to `customSongs/` are picked up automatically.

Hotkeys are **observed, never blocked**: the game still casts the skill. Presses are ignored while GW2 chat or an addon text field has focus, and only count while the notes window is open.

## Updating and uninstalling

For source builds: `git pull --ff-only && ./setup-linux.sh`. For the prebuilt version repeat the download commands and run `python3 install.py`. Your settings and custom songs are kept. To uninstall, close the game and remove `addons/dance-dance-rotation.dll` (keep `addons/dance-dance-rotation/` for your settings and songs).

## Building and testing

```bash
./setup-linux.sh --build-only        # or: ZIG=/path/to/zig ./build.sh
WINE="$HOME/.local/share/Steam/steamapps/common/Proton - Experimental/files/bin/wine" ./smoke-test.sh
```

`./build.sh` embeds the 1.8.1 data (songs, 25,887 ability names and icon ids, palette table) and 32 PNG images, runs the native C++ tests and Python tests, compiles `dist/dance-dance-rotation.dll` and checks its architecture, export and dependencies.

- Native tests decode all 155 bundled song records with the addon's C++ JSON reader and check that they match the build-time Python decoder note by note (102 unique songs, 22,194 notes). They also cover the layout arithmetic, hit windows, misses, auto-hit Weapon 1, No Miss Mode pauses, playback rate, start time, utility remapping, compact lanes, the next-abilities strip, pause/resume and song end.
- The Wine smoke test loads the real DLL with real ImGui and a simulated Nexus API, three load/unload cycles: clipboard import, starting with the first note's key, song end, assigning a hotkey (the assigned key is consumed, gameplay keys are not), Toggle Play/Stop, chat blocking and all three window shapes. It uses a separate prefix under `build/wine-smoke` and never touches the game prefix.
- `tests/render_preview.cpp` + `tools/render_preview.py` optionally rasterize the addon's ImGui draw data to a PNG for a visual check.

## Differences from the Blish HUD module

- Note movement is computed from the song clock instead of integer-millisecond frame steps, so timing does not depend on frame rate; hit grades use the same windows (33/92/142/225 ms).
- One key press starts the song and clears only the first note (the original could also clear a second note of the same type within 225 ms).
- Profession Skill 5 defaults to F5 (the original defaulted it to F4, like Profession Skill 4).
- Mouse buttons can be bound; key presses are ignored while chat is focused.
- Hit ratings appear at the top of the screen as a single fading text (optional) instead of Blish screen notifications; "Starting", "Stopped" and import messages use Nexus alerts.
- Deleting a built-in song hides it until the next addon update (the original deleted its copied file until the next module update).
- Window sizes and positions are remembered by ImGui per window shape; there is an extra Lock option and a Settings window.
- Ability icons are downloaded on demand from `assets.gw2dat.com`, the source Blish uses for these asset ids; until an icon arrives the generic note is shown.

## Troubleshooting

- **Notes never get hit**: check that each Ability Hotkey matches your game binding, that the notes window is open, and that chat is not focused.
- **No icons, only colored notes**: icons load from the network on first use per session; check connectivity, or disable *Show ability icon as note*.
- **Addon missing or disabled**: check `addons/Nexus/Nexus.log` and that your Nexus supports API v6.
- **No sound**: the original module has no sound either; there is no music, the "song" is the rotation.

## Source and credits

- [Dance Dance Rotation](https://github.com/campbt/DanceDanceRotation) by Shooper, reference tag `1.8.1`; MIT, see `LICENSE`. Songs are Snow Crows builds as packaged by the original module.
- [Nexus API](https://github.com/RaidcoreGG/Nexus-API) v6; see `vendor/Nexus-LICENSE`. Exact ImGui 1.80 sources from Nexus; see `vendor/imgui`.
- Guild Wars 2 names and art belong to ArenaNet / NCSOFT. See `THIRD_PARTY.md`.

See [provenance and implementation notes](docs/PROVENANCE.md). This is an unofficial local port; it is not listed in the Nexus library and does not self-update.
