# goodluckOS user guide

## Contents

1. [How goodluckOS runs from the microSD](#how-goodluckos-runs-from-the-microsd)
2. [Install on a microSD card](#install-on-a-microsd-card)
   1. [Back up the original card](#back-up-the-original-card)
   2. [Build the image](#build-the-image)
   3. [Flash the assembled image](#flash-the-assembled-image)
3. [Expand HOME](#expand-home)
4. [Add games and emulator cores](#add-games-and-emulator-cores)
   1. [Add a RetroArch core](#add-a-retroarch-core)
5. [Find and back up saved games](#find-and-back-up-saved-games)
6. [Controls and updates](#controls-and-updates)

## How goodluckOS runs from the microSD

When you power on the console, it boots from the microSD in **TF1**. The Firmware Builder combines the startup data from your original card with the goodluckOS kernel and two partitions. The startup data loads the Linux kernel, which starts the system and opens **Puppy**, the game and app launcher.

| Partition | What it contains |
| --- | --- |
| `linux` (ext4) | The goodluckOS system files and bundled programs, including Puppy and RetroArch |
| `HOME` (FAT32) | Your games, saves, settings and any extra cores you install |

**RetroArch** is the program Puppy launches for games configured to use it. RetroArch handles the controller, video, sound and menus. It loads a **Libretro core**, a plug-in that emulates the original console. A **ROM** is the game file the core runs. For example, when you pick an NES game in Puppy, it can launch RetroArch with the FCEUmm core and pass it your `.nes` file. Some games and ports use standalone programs instead. [Libretro core directory](https://docs.libretro.com/guides/core-list/)

## Install on a microSD card

### Back up the original card

**Linux.** Insert the original microSD and use `lsblk` to identify the whole card by its size. Replace `/dev/sdX` below with that device, for example `/dev/sdb` or `/dev/mmcblk0`. The Builder needs only the first 128 MiB for this step.

```bash
lsblk -o NAME,SIZE,MODEL,TRAN,MOUNTPOINTS
sudo dd if=/dev/sdX of=stock_sdcard.img bs=1M count=128 status=progress
```

**Windows.** In [HDD Raw Copy Tool](https://hddguru.com/software/HDD-Raw-Copy-Tool/), choose the original microSD as the source and a file on your PC as the destination. Save it in raw `.img` format. The Builder accepts a full-card image, so allow enough free space on your PC for the size of the card.

### Build the image

Open the [Firmware Builder](https://codezombie.github.io/goodluckOS/download.html) and load the original card image. Download the goodluckOS base for the detected processor and the appropriate panel. Extract the base ZIP, load the resulting `.img` into the Builder, and save the assembled image. The examples below call it `goodluckOS-final.img`.

The Firmware Builder page also links to video tutorials that walk through the process. [Windows video guide](https://www.youtube.com/watch?v=bkud4CBHvI8) · [Linux video guide](https://www.youtube.com/watch?v=3RUR5a7hDDs)

### Flash the assembled image

Flashing erases the destination. Remove the original card, insert a spare microSD, and check the destination by size before starting.

**Linux.** Run `lsblk` again with the spare card inserted. Unmount each mounted partition on that card, then write to the whole device. Replace both example device paths with the ones you identified. If the card has no mounted partitions, skip the `umount` command.

```bash
lsblk -o NAME,SIZE,MODEL,TRAN,MOUNTPOINTS
sudo umount /dev/sdX1
sudo dd if=goodluckOS-final.img of=/dev/sdX bs=4M conv=fsync status=progress
```

The Linux video tutorial shows the `dd` command without `conv=fsync`. The command above includes it so `dd` flushes pending writes to the card before it exits. If you follow the video command, run `sync` afterward and wait for it to finish before ejecting the card.

**Windows.** In balenaEtcher, choose **Flash from file** and open the assembled `.img`. Choose **Select target**, select the spare microSD, check its size, and click **Flash!**. Wait for Etcher to finish writing and verifying the image.

You can also use HDD Raw Copy Tool. Choose the assembled `.img` as the source and the spare microSD as the destination. Confirm the card's size and start the copy. If Windows asks to format a partition after flashing, cancel the prompt.

Wait for the write to finish and safely eject the card. With the console powered off, insert it in TF1 and turn the console on.

## Expand HOME

The image ships with a small HOME partition. To use the remaining space on a larger card, back up HOME separately, open **Resize Home** from the **System** tab in Puppy, and follow the on-screen prompts. Keep the console powered while it restarts and resizes the partition. After boot, reopen the tool to check the result.

## Add games and emulator cores

**Linux.** Open the **HOME** volume in your file manager. If the card is mounted locally, you can also copy games from the terminal. Replace `/media/$USER/HOME` with the mount point listed by `lsblk`.

```bash
cp -- "/path/to/game.nes" "/media/$USER/HOME/roms/nes/"
```

**Windows.** Open **HOME** under **This PC** in File Explorer and copy the files into the matching system folder, such as `roms\nes`, `roms\snes`, or `roms\psx`.

On either OS, you can also connect the console over USB in MTP mode and open **GA36MB → HOME** in your file manager. Reopen Puppy to refresh the list after copying. The [README](README.md#how-do-i-add-games) also covers the optional second card in TF2.

### Add a RetroArch core

1. Look up the core for your system in the Libretro core directory. Check its supported game formats and whether it requires BIOS files. [Core directory](https://docs.libretro.com/guides/core-list/) · [BIOS documentation](https://docs.libretro.com/guides/bios/)
2. Download a compatible **Linux ARM hard-float (armhf)** build and extract the `*_libretro.so` file. Use the armhf build even if you download it on a Windows PC, since the core runs on the console. [Libretro armhf downloads](https://buildbot.libretro.com/nightly/linux/armhf/latest/) · [Official core installation guide](https://docs.libretro.com/guides/download-cores/#installing-cores-manually)
3. Create `cores` on **HOME** if needed, then copy the `.so` there. On the console, the file will be under `/home/player/cores/`.
4. Add an `[ARCHIVE]` entry to HOME's `apps.puppy`. Set the ROM directory and file extensions, and use a launch command such as `retroarch -L /home/player/cores/example_libretro.so %s`. Replace the example filename with the actual core filename. Puppy replaces `%s` with the selected game path. Reopen Puppy to load the new entry. [Puppy archive configuration](rootfs/package/puppy/PUPPY.md#archive-entries-archive)

## Find and back up saved games

By default, RetroArch stores saves on the **HOME** partition in the first microSD card, even when you launch a ROM from TF2.

| What you saved | Folder on HOME |
| --- | --- |
| In-game saves, such as cartridge saves or virtual memory cards | `.config/retroarch/saves/` |
| Save states created from the RetroArch menu | `.config/retroarch/states/` |

RetroArch can organize these files in subfolders by core and ROM folder. If you cannot see `.config` over USB, shut down the console and open the card directly on your PC. On Linux, enable **Show Hidden Files** in your file manager. On Windows, open **HOME** in File Explorer and type `.config\retroarch` after the drive letter in the address bar, for example `E:\.config\retroarch`.

Exit the game normally before copying its saves. To back them up, copy both folders, including their subfolders, to your PC.

## Controls and updates

See the [hotkey reference](README.md#hotkeys) for brightness, audio output, and exiting games.

A full-image flash replaces HOME. Before flashing an update, use your file manager on Linux or File Explorer on Windows to copy the contents of HOME to a folder on your PC. Include hidden files and folders, such as `.config`, to preserve your settings. After flashing, expand HOME if needed and copy your files back. An [in-place update mechanism](https://github.com/CodeZombie/goodluckOS/issues/25) is tracked separately.
