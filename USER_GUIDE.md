# goodluckOS user guide

## Install on a microSD card

1. Save an image of the first 128 MiB of the original microSD card. Keep this backup for future builds.
2. Open the [Firmware Builder](https://codezombie.github.io/goodluckOS/download.html) and load the backup. Use the base image for the processor and panel selected in the Builder.
3. Download the assembled image and write it to a spare microSD with a disk image writer. Double-check the target card, since flashing replaces its contents.
4. With the console powered off, insert the new card in TF1 and turn it on.

## Expand HOME

The image ships with a small HOME partition. To use the remaining space on a larger card, back up HOME separately, open **Resize Home** from the **System** tab in Puppy, and follow the on-screen prompts. Keep the console powered while it restarts and resizes the partition. After boot, reopen the tool to check the result.

## Add games and emulator cores

On your PC, open the card's **HOME** partition or connect the console over USB in MTP mode. Put your games in the appropriate folders, such as `roms/nes`, `roms/snes`, and `roms/psx`. Reopen Puppy to refresh the list. The [README](README.md#how-do-i-add-games) also covers the optional second card in TF2.

To add an emulator, put a compatible ARM hard-float libretro core in HOME's `cores` directory. Add an `[ARCHIVE]` entry to `apps.puppy` that points to your games and launches RetroArch with that core. See the [Puppy configuration reference](rootfs/package/puppy/PUPPY.md#archive-entries-archive) for the entry format.

## Controls and updates

See the [hotkey reference](README.md#hotkeys) for brightness, audio output, and exiting games.

A full-image flash replaces HOME. Back up HOME before flashing an update, then restore your files to the new HOME partition. An [in-place update mechanism](https://github.com/CodeZombie/goodluckOS/issues/25) is tracked separately.
