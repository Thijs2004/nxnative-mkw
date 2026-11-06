# nxnative-mkw: install tutorial

nxnative-mkw runs Mario Kart World version 1.8.0 (update v917504) on an iPhone or a Mac. This guide takes you from your own copy of the game to playing it on an iPhone: you build an app on a Mac, install it on the iPhone, and copy the game files across. To play on the Mac itself, which is simpler, follow Steps 1 to 3 and then [Playing on a Mac](#playing-on-a-mac).

**This repository has no game files, keys or ready-made apps.** You need your own dump of the game and its update. The app and the `RomFS.img` file you make here contain your game, so keep them to yourself.

**What to expect:** on an iPhone 16 Pro Max (8 GB) a race holds a steady 30 fps with the **Medium** preset. The phone gets warm, and the game frees other apps' memory itself before it loads. Online play, local wireless and GameChat are not available. Details are in the [game's note](dm/games/0400C3F00006E000/README.md#status).

## What you need

| | |
|---|---|
| **The game** | Your own dump of Mario Kart World: the base game **plus the 1.8.0 update (v917504)** |
| **A Mac** | Apple silicon, Xcode 27 (tested with Xcode 27.0), and about 55 GB free for the iPhone app (22 GB dump, 23.5 GB `RomFS.img`, 6 GB shaders and build files), plus 23 GB if you also build the Mac app |
| **An iPhone** | Tested on an iPhone 16 Pro Max, with about 30 GB free |
| **A way to install apps** | A paid developer account (best), or a free Apple account with SideStore and [iRAM+](https://github.com/NexSideloading/iRAM-Plus), or LiveContainer: see [Step 5](#step-5-install-the-app) for which is best |

## Step 1: Check your game version

Only this exact update works. The code patches are made for these modules, so another version will not run.

| | |
|---|---|
| Title ID | `0400C3F00006E000` |
| Update | 1.8.0 (v917504, `ResourceSizeTable.Product.180` in its RomFS) |
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
| `0400C3F00006E000_build/extract/v917504/RomFS.img` | about 23.5 GB | The game files |

Once `RomFS.img` is on the iPhone you can delete the Mac's copy, and move the dump to another drive. Keep the 6 GB of shaders in the build folder if you will build updates: they save the long first compile.

The memory settings an 8 GB iPhone needs are built into the app ([`env_ios.txt`](dm/games/0400C3F00006E000/env_ios.txt)); you don't have to set anything.

## Step 5: Install the app

The app needs the **Increased Memory Limit** entitlement. It also asks for **Extended Virtual Addressing** ([`app.entitlements`](dm/games/0400C3F00006E000/app.entitlements)); that one helps but is not required: on an iPhone 16 Pro Max with iOS 27 the game also starts without it.

The app contains a small **RAM helper** (an app extension, the technique of MeloNX 2.7). It gives the game about 1.5 GB more room before iOS closes it. The game runs without it, but less smoothly, and it switches to a memory layout for that case by itself.

### The best ways, from best to least good

| | Route | Account | RAM helper | What to expect (iPhone 16 Pro Max, Medium preset) |
|---|---|---|---|---|
| **1** | Direct install, signed with a developer account | paid | yes | a steady 30 fps, 0 to 2 dropped frames per 300 |
| **2** | SideStore, keeping the app extension | free | yes, if SideStore keeps it | the same as route 1 |
| **3** | SideStore without the extension, or LiveContainer | free | no | about 28 to 30 fps, more dropped frames; use Medium or Low |

### Route 1: a paid developer account

Enable **Increased Memory Limit** and **Extended Virtual Addressing** for the app's App ID (and an App ID for the extension, `<your app ID>.RamDonor`), sign the IPA with Xcode, Sideloadly or another tool that signs with your team, and install it.

### Route 2: a free Apple account with SideStore (recommended for free accounts)

1. Get SideStore on the iPhone. Without a computer, [SideInstaller](https://sideinstaller.net/) can install it for you. Use SideInstaller only for SideStore itself: the game must be signed with **your own** Apple account, not with an enterprise certificate.
2. Install `MarioKartWorld.ipa` with SideStore. If SideStore asks whether to keep the app extension, keep it: the extension is the RAM helper. It takes a second App ID (a free account has 10 a week).
3. Install [iRAM+](https://github.com/NexSideloading/iRAM-Plus), sign in with the same Apple account, select **Mario Kart World**, and add **Increased Memory Limit** (and **Extended Virtual Addressing**, if it offers it).
4. Reinstall `MarioKartWorld.ipa` through SideStore, so its new signature carries the entitlements.

A free account's apps must be refreshed in SideStore every 7 days.

### Route 3: LiveContainer, or SideStore without the extension

LiveContainer runs the game inside its own process, so app extensions can't start there and the RAM helper is not used. Apply iRAM+ to **LiveContainer itself** (the entitlements must be LiveContainer's), then install the IPA in LiveContainer. Pick the **Medium** or **Low** preset.

### Check whether the RAM helper works

After a first start, open `log.txt` in the app's Documents folder (Step 6 shows where):

| The log says | Meaning |
|---|---|
| `ram donor: 7168 MB of 7168 MB from 3 donor(s)` | the helper works (routes 1 and 2) |
| `no donor extension in this build` or `ram donor: cannot load the extension` | no helper (route 3); the next line says `no RAM helper: the guest memory file takes the heap ranges of DM_SWAP_HEAP_NOHELPER` |

Routes 2 and 3 were tested by signing the app without the extension and without Extended Virtual Addressing, not with a real free account yet.

Don't start the game yet.

## Step 6: Put the game files on the iPhone

The game files go in the app's own **Documents** folder.

1. Get `RomFS.img` onto the iPhone: a USB-C drive formatted as APFS or exFAT (FAT32 can't hold a file this big), AirDrop to the Files app, or Finder with the iPhone connected (select the iPhone, open the **Files** tab, and drag the file onto **Mario Kart World**).
2. In the Files app, open **On My iPhone → Mario Kart World** and put `RomFS.img` there. In LiveContainer the folder is **On My iPhone → LiveContainer → Data → Application → (the game's folder) → Documents** instead; LiveContainer's settings for the app show which folder it is. If the file is already on the iPhone, use **Move** rather than **Copy**: a copy needs another 23.5 GB free.
3. Wait for it to finish. Don't launch the game while the file is still arriving.
4. Keep at least 5 GB free on top of that. While it runs, the game keeps part of its memory in a temporary file.

## Step 7: Play

1. The game uses almost all of an 8 GB phone's memory. When you tap Play it first frees the memory other apps still hold (about 2 seconds), so a start right after using other apps is as smooth as a fresh one (`DM_MEM_PURGE_MB=0` in env.txt turns this off).
2. Start the game. A settings screen comes first. Tap **Check** next to **Check iPhone**: it looks up your iPhone's model and picks the best preset for it (15 Pro / 15 Pro Max / 16 / 16 Plus / 16e / 17e: Low; 16 Pro / 16 Pro Max / 17: Medium; Air / 17 Pro / 17 Pro Max: High; 18 Pro and newer with 12 GB: Full; only the 16 Pro Max is tested). Or pick a **Graphics preset** yourself:

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
| The game closes at once, `reserve guest AS` in the log | The phone gives the app too little address space: add Extended Virtual Addressing (Step 5) and reinstall |
| It runs, but with more stutter than expected | Check whether the RAM helper works (Step 5); without it, use the Medium or Low preset, restart the phone before playing, and let it cool down |
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
