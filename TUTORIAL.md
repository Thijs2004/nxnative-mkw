# nxnative-mkw: install tutorial

nxnative-mkw runs Mario Kart World with update v917504 on an iPhone or a Mac. This guide takes you from your own copy of the game to playing it on an iPhone: you build an app on a Mac, install it on the iPhone, and copy the game files across. To play on the Mac itself, which is simpler, follow Steps 1 to 3 and then [Playing on a Mac](#playing-on-a-mac).

**This repository has no game files, keys or ready-made apps.** You need your own dump of the game and its update. The app and the `RomFS.img` file you make here contain your game, so keep them to yourself.

**What to expect:** on an iPhone 16 Pro Max (8 GB) a race holds a steady 30 fps with the **Medium** preset. The phone gets warm, and runs are smoother right after a restart of the phone. Online play, local wireless and GameChat are not available. Details are in the [game's note](dm/games/0400C3F00006E000/README.md#status).

## What you need

| | |
|---|---|
| **The game** | Your own dump of Mario Kart World: the base game **plus update v917504** |
| **A Mac** | Apple silicon, Xcode 27 (tested with Xcode 27.0), and about 60 GB free (22 GB for the dump, the rest for the build) |
| **An iPhone** | Tested on an iPhone 16 Pro Max, with about 30 GB free |
| **A way to install apps** | A sideloading tool or developer account that can give the app the **Increased Memory Limit** and **Extended Virtual Addressing** entitlements |

## Step 1: Check your game version

Only this exact update works. The code patches are made for these modules, so another version will not run.

| | |
|---|---|
| Title ID | `0400C3F00006E000` |
| Update | v917504 |
| `main` build ID | `d00e4f399e411d0613aa8d26b1c02fef` |
| `subsdk0` build ID | `746302f49f095cfca3d2188423c2bd77` |
| `subsdk1` build ID | `8dec9c0b1bed821364e77b10a07b9a28` |
| `sdk` build ID | `7a68b13d88183767fb9c7babffe89f3d` |

## Step 2: Put the game files in a folder

Make a folder on the Mac and lay out your dump exactly like this. The folder's name and location are up to you, but the `base`, `updates`, `exefs` and `romfs` names are not, and `917504` must be the update's number:

```
Mario Kart World/
  base/
    exefs/
      main
      main.npdm
      rtld
      sdk
      subsdk0
    romfs/
      (the base game's files and folders; may stay empty, the update's RomFS is whole)
  updates/
    917504/
      exefs/
        main
        main.npdm
        rtld
        sdk
        subsdk0
        subsdk1
      romfs/
        (all of the update's files and folders)
```

Check the update's `main` against the table in Step 1. In Terminal, this prints its build ID, which should match exactly:

```bash
xxd -s 0x40 -l 16 -p "$HOME/Games/Mario Kart World/updates/917504/exefs/main"
```

Change the path if you put the folder somewhere else. To check the other modules, replace `main` at the end of the path with `subsdk0`, `subsdk1` and `sdk`.

## Step 3: Set up the Mac

You only do this once.

1. Install Xcode 27 and open it once. Add its iOS platform in **Xcode → Settings → Components**.
2. Install [Homebrew](https://brew.sh) and Python 3.12 or newer (an arm64 Python, not an Intel Homebrew's).
3. In Terminal, select Xcode and get the Metal tools:

   ```bash
   sudo xcode-select -s /Applications/Xcode.app
   ```

   ```bash
   xcodebuild -downloadComponent MetalToolchain
   ```

4. Download this repository and go into it:

   ```bash
   git clone https://github.com/Thijs2004/nxnative-mkw.git
   ```

   ```bash
   cd nxnative-mkw
   ```

5. Install the build tools:

   ```bash
   brew install xcodegen zstd glslang
   ```

   ```bash
   brew install --cask dotnet-sdk
   ```

6. Make a Python environment for the build and install its packages:

   ```bash
   python3 -m venv .venv
   ```

   ```bash
   source .venv/bin/activate
   ```

   ```bash
   python3 -m pip install lz4 "capstone>=5,<6" fonttools nsz zbic "backports.zstd; python_version < '3.14'"
   ```

If you open a new Terminal window later, go back into the repository folder and run `source .venv/bin/activate` again before building.

## Step 4: Build the app

From the repository folder, run this with the path to your game folder:

```bash
python3 scripts/build_ipa_from_nsp.py "$HOME/Games/Mario Kart World" --platform ios --romfs separate
```

No `prod.keys` is needed for a dump. The first build compiles about 11,600 shaders, which can take up to an hour, and your Mac may be slow during that time. Later builds reuse them and take a few minutes.

The shaders were tested with the Metal compiler of Xcode 27.0. With another Xcode the build stops with a message about the Metal compiler: add `--allow-other-metal` to the command to build anyway.

When it finishes, your game folder holds the two files you need:

| File | Size | What it is |
|---|---|---|
| `MarioKartWorld.ipa` | about 670 MB | The app |
| `0400C3F00006E000_build/extract/v917504/RomFS.img` | about 22 GB | The game files |

The memory settings an 8 GB iPhone needs are built into the app ([`env_ios.txt`](dm/games/0400C3F00006E000/env_ios.txt)); you don't have to set anything.

## Step 5: Install the app

Install `MarioKartWorld.ipa` with your sideloading tool or developer account. The app needs the **Increased Memory Limit** and **Extended Virtual Addressing** entitlements ([`app.entitlements`](dm/games/0400C3F00006E000/app.entitlements) if your tool asks for a file): without them the game runs out of memory in a race.

The app also contains a small helper extension that lends it extra memory on phones where the system allows that. If your tool can't sign app extensions, the game still runs without it, with less headroom.

Don't start the game yet.

## Step 6: Put the game files on the iPhone

The game files go in the app's own **Documents** folder.

1. Get `RomFS.img` onto the iPhone: a USB-C drive formatted as APFS or exFAT (FAT32 can't hold a file this big), AirDrop to the Files app, or Finder with the iPhone connected (select the iPhone, open the **Files** tab, and drag the file onto **Mario Kart World**).
2. In the Files app, open **On My iPhone → Mario Kart World** and put `RomFS.img` there. If the file is already on the iPhone, use **Move** rather than **Copy**: a copy needs another 22 GB free.
3. Wait for it to finish. Don't launch the game while the file is still arriving.
4. Keep at least 5 GB free on top of that. While it runs, the game keeps part of its memory in a temporary file.

## Step 7: Play

1. **Restart the iPhone** and close other apps before playing. The game uses almost all of an 8 GB phone's memory, and a fresh phone has the most free.
2. Start the game. A settings screen comes first. Pick a **Graphics preset**:

   | Preset | Textures | Render scale | Frame rate |
   |---|---|---|---|
   | Full | Full | 100 % | Locked 60 (slow motion where the phone can't keep up) |
   | High | High | 80 % | Game decides |
   | **Medium** (recommended) | Medium | 65 % | Locked 30 |
   | Low | Low | 50 % | Locked 30 |

   Changing a single setting by hand switches the preset to **Custom**.
3. Tap **Play**. The first launch builds its pipelines behind a progress screen.

A connected controller works; touch controls are on screen. Saves are in the app's Documents folder.

## Playing on a Mac

The game also runs on a Mac with Apple silicon. There is nothing to install or copy: the Mac app carries the game files inside it.

1. Follow Steps 1 to 3 above.
2. Build the Mac app, from the repository folder, with the path to your game folder:

   ```bash
   python3 scripts/build_ipa_from_nsp.py "$HOME/Games/Mario Kart World" --platform mac-app
   ```

3. Open `Mario Kart World.app` from your game folder. It is about 23 GB. The first time you open it, macOS can sit silently for over a minute while it scans the app.
4. A settings window appears before the game starts.

**What to expect:** on a MacBook Air M4 with 16 GB a race runs at 60 fps. Close other apps before you play. You need a controller.

The log is at `~/Library/Logs/nxnative/0400C3F00006E000.log`.

## If something goes wrong

The iPhone app writes a log to `log.txt` in its Documents folder, and keeps the previous run's as `log.prev.txt`. Check it first.

| What you see | What to do |
|---|---|
| The game doesn't start | Check that `RomFS.img` is directly in the app's Documents folder and has finished copying, and that it came from the same build as the app |
| `is not the image this build needs` in the log | The file doesn't match this app. Copy over the `RomFS.img` from the same build as your IPA |
| The game closes during a race | Restart the phone, free up storage (Step 6), and use the Medium or Low preset. Check that the app has the Increased Memory Limit entitlement |
| Stutter that comes and goes | The phone is short of memory: restart it before playing, and let it cool down |
| The build stops at the shaders | Read the message: with another Xcode than 27.0, add `--allow-other-metal` |
| Wrong version or a crash at boot | Compare your build IDs with Step 1 |

## Updating

1. Get the latest code with `git pull`.
2. Run the build command in Step 4 again.
3. Install the new `MarioKartWorld.ipa` over the old one.

The game's data stays, so you don't need to copy `RomFS.img` again unless your dump changed. Deleting the app deletes `RomFS.img` and your saves.

## More

- [The game's note](dm/games/0400C3F00006E000/README.md): how it works, its status and what is still open
- [Main README](README.md): the nxnative project and its other games
