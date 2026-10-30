# Reference and implementation notes

Reference package: `dance-dance.bhm`, Blish HUD module **Dance Dance Rotation 1.8.1**, namespace `com.shooper.ddr`.
The reference .NET assembly is not included in this repository.

- Package SHA-256: `f6eed7bec32a110252b751e8e089b877afe79146e2b3d7558debb89cc5d31d77`.
- `DanceDanceRotationModule.dll` SHA-256: `8559ef61a1725111b20a87e8467043086df8f68055d25801748fa1bbac2108b0`, MVID `6e916819-a64f-44e6-8329-146d10da5fdd`.
- Source used to interpret behavior: [campbt/DanceDanceRotation tag 1.8.1](https://github.com/campbt/DanceDanceRotation/tree/1.8.1), commit `712f919c161fcbcd80ee0e66e1a3efef15600f0e`, especially `Views/NotesContainer.cs`, `Storage/SongTranslator.cs`, `Storage/SongRepo.cs`, `Storage/ModuleSettings.cs` and `Views/SongInfoView.cs`.

## Static inspection (REA 6.7.0)

REA classified the reference as an AnyCPU .NET Framework 4.8 CIL assembly with complete metadata: 83 types, 855 methods, 514 fields and 533 `ldstr` user strings (Evidence `ev_386a9268…aac24` artifact inspection, `ev_815c167a…dd12` member inspection). The type inventory matches the 1.8.1 source (NotesContainer with WindowInfo, ActiveNote, HitText and AbilityIcon; SongRepo; SongTranslator; ModuleSettings; the five views). 508 of the 533 user strings occur in the 1.8.1 sources; the remaining 25 are compiler-generated anonymous-type and interpolated-string formats or concatenated log text. The packaged `ref/` data files are semantically identical (parsed JSON) to the tag's data files. Execution of the original module and a full instruction-by-instruction equivalence were not established.

## Port

`src/model.hpp`, `src/json.hpp`, `src/song_json.hpp` and `src/engine.hpp` are platform-independent and tested natively. `src/addon.cpp` implements the Nexus lifecycle, ImGui windows, key observation through the Nexus WndProc chain, MumbleLink profession and chat-focus reading, textures, the song folder and settings.

- **Songs.** `tools/generate_data.py` decodes the 155 bundled records with the SongTranslator rules (unknown note names become Unknown, numeric strings accepted as numbers, half-to-even rounding, later records replace earlier ones of the same name) into 102 songs. Custom songs are decoded at runtime by the C++ reader; the native test checks both decoders agree on every note.
- **Layout.** `compute_layout` follows `WindowInfo.Recalculate` integer arithmetic (perfect position at 14%, 15 px vertical padding, lane spacing, note sizes, spawn/destroy positions, time to reach the target, collision time). The reference read the previous frame's note width for the next-abilities strip; repeated layout passes converge to the final value, which is used directly. Target circles in the ability-bar layout are aligned with the lanes.
- **Timing.** The reference moved notes by `speed × ElapsedGameTime.Milliseconds` per frame (whole milliseconds) and spawned at most one note per frame. Here a note's position is `spawn + speed × (songClock − time / rate)`, which is the same model evaluated exactly. Hit windows stay 33/92/142/225 ms around Perfect; No Miss Mode pauses exactly at Perfect; auto-hit Weapon 1 notes clear at Perfect; the first note is prepared on the target when Start with first skill or No Miss Mode is on, otherwise it reaches the target 1 s after Play (including the reference's behavior for travel times under 1 s).
- **Input.** Blish KeyBindings are replaced by observing `WM_KEYDOWN`/`WM_SYSKEYDOWN` and mouse buttons 3–5 in the Nexus WndProc chain with exact Ctrl/Alt/Shift matching, ignoring auto-repeat. Messages are passed to the game. Presses are timestamped with QueryPerformanceCounter and graded at that time. Assigning a hotkey consumes that one key press.
- **Persistence.** `addons/dance-dance-rotation/settings.txt` (settings, hotkeys, selected song, per-song data) and `customSongs/*.json`, watched with a change notification polled once per frame (no addon threads).

No addon-owned threads or texture callbacks exist; the change notification handle and all Nexus registrations are released on unload.
