# Mario Kart World (`0400C3F00006E000`), update v917504

A Switch 2 game, SDK 21.4.0 (`Ounce`), NVN2 API 231.39. Nintendo EPD's engine (sead / agl / gsys), modules `main`, `subsdk0`, `subsdk1` and `sdk`. **Runs on an iPhone 16 Pro Max (8 GB) and on a MacBook Air M4 (16 GB).** Nothing from the game is in this repository: you need your own dump of the base game and update v917504. The [install tutorial](../../../TUTORIAL.md) goes through every step.

## Required game version

| | |
|---|---|
| Title ID | `0400C3F00006E000` |
| Update | v917504 |
| `main` build ID | `d00e4f399e411d0613aa8d26b1c02fef` |
| `subsdk0` build ID | `746302f49f095cfca3d2188423c2bd77` |
| `subsdk1` build ID | `8dec9c0b1bed821364e77b10a07b9a28` |
| `sdk` build ID | `7a68b13d88183767fb9c7babffe89f3d` |

The patches ([`patches.py`](patches.py)) are made for these exact modules.

## Status

| | |
|---|---|
| Grand Prix, VS races | Play on iPhone and Mac |
| Frame rate, iPhone 16 Pro Max | A steady 30 fps in races with the **Medium** preset (Locked 30, 65 % render scale, Medium textures), also with a warm phone. Runs are smoother right after a restart of the phone |
| Frame rate, Mac (M4, 16 GB) | 60 fps |
| Picture | Right in the races tested |
| Online, local wireless, GameChat | Not available |
| Save data | In the app's Documents (iPhone) or `~/Library/Application Support/nxnative/` (Mac) |

## What it does

- **Game version:** the v917504 profile ([`game.tsv`](game.tsv)): build IDs, modules, memory layout, NVN API 231.39, and the Switch 2 system services this update calls at boot (GameChat, camera, graphics-system memory, …) as stubs.
- **Render scale:** the game has its own dynamic resolution (a scaler at `main+0x25b6bb0`, gated at `main+0x474a24` / `0x474a48`). The patches open its gates and feed it the launcher's Render scale, so the 3D scene renders smaller and the game upscales it with a sharp HUD.
- **Frame rate lock:** a stub writes the game's present interval from the launcher's Frame rate (Locked 30 / 60, or the game decides). It pauses while a movie plays: the pre-race movie under the 30 fps lock cut its sound out every 0.74 s.
- **Graphics presets (iPhone):** Full / High / Medium / Low set texture quality, render scale, frame rate, upscaling and the performance switches together.
- **Memory on an 8 GB iPhone:**
  - the game's large GPU memory pools live in a file the system can page out ([`env_ios.txt`](env_ios.txt));
  - Texture quality leaves out the finest levels of large compressed textures;
  - Free texture memory marks the storage of uploaded textures as cold, so the system pages it out first (nothing is lost);
  - a helper extension lends the app extra memory where the system allows it ([`app.entitlements`](app.entitlements): Increased Memory Limit and Extended Virtual Addressing).
- **Native mesh shaders:** the game's task and mesh shaders draw through Metal mesh pipelines (after Plumber Racer 2).

## Open

- **Pop-in:** trees and objects beside the track appear late, most on Moo Moo Meadows. Not found yet; part of it may be the game's own level-of-detail switching.
- **iPhone memory:** an 8 GB phone is at its limit. Stutter comes back when the phone has less free memory (other apps, many races in a row).
- **Full preset:** Locked 60 runs in slow motion where the phone can't keep up; races reach about 45 fps.

## Credits

- [nxnative](https://github.com/seedds/nxnative) by seedds: the whole engine this runs on.
- [kartnative](https://github.com/pahorema/kartnative) by pahorema: terrain textures at their first sample, the memory map limit, sparse texture checks.
- [Plumber Racer 2](https://github.com/cheemu21/plumber-racer-2) by cheemu21: native mesh shaders, texture give-back, the entitlements file.
- The RAM helper extension follows the technique of MeloNX 2.7.
