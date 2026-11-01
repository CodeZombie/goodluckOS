# goodluckOS
An uncompromisingly fast, small, modern, and feature-rich custom firmware for A23/A33-based handheld consoles.

Latest release: [0.1.0 [Oct 10, 2026]](https://codezombie.github.io/goodluckOS/download.html)

> [!IMPORTANT]
> **goodluckOS is BETA software. You will run into bugs!**
>
> Please visit [The Firmware Builder](https://codezombie.github.io/goodluckOS/download.html). This will walk you through the steps of identifying if you have a compatible unit, and building a flashable goodluckOS image.

# Screenshots
Put screenshots here.

# Features 
| Feature | Stock Firmware | goodluckOS |
| ------- | -------------- | ---------- |
| Linux Version | Linux-Sunxi 3.4 | Mainline Linux 7.2 |
| Optimized Firmware | No, janky EmuElec fork | Yes, built from scratch for maximum performance |
| Optimized Binaries | No | Yes, everything compiled for armhf neon-vfpv4 |
| Boot Time | 50 seconds | 10 seconds |
| Game Launch Time | ~4 seconds | ~1 second |
| Minimum microSD Size | 16gb | 512mb |
| Maximum microSD Size | 64gb/128gb | No limit |
| TF-2 Slot | No | Yes (on supported hardware) |
| USB TTY | No | Yes |
| USB MTP | No | Yes. Plug your console into your PC and copy games without unplugging the microSD card |
| Zram | No | Yes, enabled by default |
| Swap | Yes, which will kill your microSD card | No, Zram is used instead |
| Accelerated Graphics | Yes, with old sunxi driver | Yes, with modern Lima driver |
| Speaker Audio | Yes | Yes |
| Headphone Audio | Yes | Yes |
| FN Features | No, FN button does nothing | FN button is a swiss-army knife of functionality. Check the [Hotkeys](#hotkeys) section for details |
| Blue LED | Always on, annoying | Only on when the screen is off |
| Retroarch | Yes | Yes |
| Libretro Cores | Older, unoptimized cores | Modern cores |
| Launcher | EmulationStation (slow) | Puppy (fast) (Emulation Station as an optional package coming soon) |
| Autostart Apps on Boot | No | Yes |
| Ports | No | Yes, [Half Life 1](https://github.com/CodeZombie/glOSports-half-life) is ported and playable. More coming soon |
| In-app Overlay | No | Yes. Volume, brightness and audio output changes show over any app, and the battery level can stay in the corner (System Settings) |
| Mountable SD Card | No, MBR is broken by default. | Yes, goodluckOS ships with a functioning MBR |
| Built-in Games | Thousands of illegal roms, so sharing the firmware is technically illegal | Comes with Chocolate Doom + a shareware doom.wad. Completely legal to distribute |
| GPL Compliant | No | Yes |

### Hotkeys
Hotkeys are quick button-combos that you can press from anywhere to perform some action.

| Buttons | Action |
| ------- | ------- |
| FN + VOL UP/DOWN | Adjust screen brightness |
| FN + DOWN | Toggle between speaker and headphone audio output |
| FN + UP | View live CPU/FPS stats |
| START + SELECT | Gracefully exit the current application | 
| FN + START + SELECT | Kill (-9) the current application |
| POWER (short press) | Turns off the screen |

### USB TTY
goodluckOS gives you a direct line into the operating system from your desktop PC so you can edit system files, monitor processes, debug configurations, etc.

To use it:
1. Plug your console into a Linux PC with a usb cable via the OTG port.
2. Open a terminal and run `sudo screen /dev/ttyACM* 115200`
3. When prompted, enter the credentials (username: `root`, password: `root`)

## How Do I Install goodluckOS?
Check out the [Firmware Builder](https://codezombie.github.io/goodluckOS/download.html) for instructions and video guides.

You'll need a backup of at least the first 128mb of the original microSD card that came with your device.

Don't have the original microSD card? Open an issue here on github, we have microSD card dumps for several hardware variants, and one of them may work for you. We'll get you sorted out.

## What still needs to be done
- Headphone auto-detection. We have reason to believe some GA36-MB variants simply lack the hardware to do this.
- More LibRetro cores
- Fine tune existing LibRetro cores for best performance.
- Portmaster (or equivalent)
- EmulationStation (as an optional replacement for Puppy)
- USB Networking
- CPU Overclocking

## Supported Devices
Every known variant of the GA36-MB is supported. There are many more Allwinner A23/A33 devices that will almost certainly work, but without the hardware it's hard to say for certain.

### Display Panels
The GA36-MB family of devices ship with at least two different display panels. gooduckOS supports both, but we haven't yet found a good way to determine which panel you have ahead of time. For now, just try both until one works.

If the Firmware Builder tells you your device is not supported, you may have a new variant we've never seen before. In that case, send us your script.bin file so we can verify that goodluckOS is safe for your device. We'll update you when we support it.

#### Coming Soon:
- R36MAX
- F35v
- Other A23/A33-based devices

Know of another Allwinner-based handheld not listed on this page? Open a new [Issue](https://github.com/CodeZombie/goodluckOS/issues) and let me know!

If you have one such device, and some spare time, leave an issue on Github and we'll work with you to build support for it, and get it running on your hardware.

Alternatively, consider contributing to the [Device Fund](https://ko-fi.com/jeremyclark) so we can purchase new consoles and port goodluckOS to them.

## Puppy
Puppy is goodluckOS' application launcher. It's what you see when you start goodluckOS. It was designed to be simple, flexible, and most importantly: fast.

Puppy works with appd to ensure it's never running when you launch an application. This ensures all of your RAM and CPU resources are available to your game, not an idle launcher.

Puppy uses a ini-formatted `apps.puppy` files to populate it's application list. 

Add a new entry by modifying the `apps.puppy` file in your HOME partition. Take a look at the comments at the top of that file for syntax/examples

## How do I add games?
Plug the console into your PC with a USB cable and open `GA36MB -> HOME` (Windows: This PC; Linux: your file manager; macOS needs an MTP app such as [OpenMTP](https://openmtp.ganeshrvel.com/)). Or plug the SD card into your PC and open up the HOME partition. In there you'll find a `roms` folder with a few subfolders for each system. Add your roms to those.

If the console was connected to a charger when it booted (no PC), restart it before plugging it into a PC to get MTP; the serial console works either way. Games copied while Puppy is open show up after you reopen it.

To add custom art to Puppy, add an image file into the `icons` folder in your rom folder with the same name as the rom file. (eg. if your game is `roms/snes/super-mario.smc`, your image would be `roms/snes/icons/super-mario.png`)

### Second card (TF-2 slot)
A FAT32/exFAT card in the second slot is found when the console starts, and its games show up in each system's tab. Put them in a `roms` folder with the same system folders as HOME (`roms/snes`, `roms/psx`, `roms/gba`...), and covers in each folder's `icons`. A card with no `roms` folder gets the folders created the first time it's mounted; existing folders and files are never touched.

Puppy finds games by folder, not by file type: games loose on the card, or in the wrong system's folder, won't show up or won't start. exFAT cards (most cards over 32 GB come that way) aren't read yet, so format the card as FAT32.

### Can I add my own archives?
You sure can!

To add a new emulator, find an appropriate armhf libretro core .so file. Add it to either:
1. The `/usr/lib/libretro` folder in the `linux` partition, or
2. Anywhere in your HOME partition

Then modify `HOME -> apps.puppy` with a new `[ARCHIVE]` entry, pointing the COMMAND to your new libretro core. Take a look at `HOME -> apps.puppy` for an exmaple.

## Does goodluckOS come with any games?
goodluckOS ships with Chocolate Doom and a shareware Doom wad.

goodluckOS will never be distributed with unauthorized copyright-protected materials. If you own the copyright to any games (or know of any permissively-licensed/CC games) that you think might make a good fit, please open an Issue! We would love to include more small, high-quality games with the OS by default.

## About Ports
Some subset of portmaster games could theoretically work, however I want to tamper your expectations a little bit before we all start getting too excited.
The Allwinner A23/A33 is a 32-bit SoC. This means that 64-bit software simply _cannot_ run on it. There are a number of popular android ports that exist only as aarch64 (64-bit) distributions. Those will never work. Additionally, even with ZRAM there, the device is still heavily RAM-limited, so unoptimized games (I'm looking at you, Balatro) will struggle to run without patching.

With that said, most of the games people actually care about (Stardew Valley, Half-Life 1, n64 re-comps, Android GameMaker games, OpenMW, OpenRCT2, + more) are 32-bit games, and therefore should theoretically work if anyone cares to create 32-bit ports.

# Development

## How Do I Build It?
GoodluckOS uses a novel four-step containerized build system featuring Buildroot. This means the OS can be configured and built extremely easily and portably with no extra dependencies at all besides Docker and Docker Compose (or Podman).

Take a look at [BUILD.MD](BUILD.md) for instructions and guidelines.

## Design Philosophy
goodluckOS has been designed with a few core principles:
- The core OS will ship with at most one tool for any job. Duplicate functionality is duplicate bugs.
- goodluckOS should strive to be as battery-efficient as possible. If a not-strictly-necessary pretty UI feature uses more resources than an uglier alternative, the uglier alternative will be preferred.
- Do not reinvent the wheel. Opt to use existing packages instead of writing them from scratch so long as they don't conflict with the previous principles.
- Anything not usable by most people should be distributed as an installable package instead of being baked into the core OS.

Please keep those in mind if you intend for your work to be merged upstream :)

We're currently in the process of choosing a package-management solution for goodluckOS, so if your application/feature is not approved for merging upstream please feel free to package it up as an installable application and we would be happy to advertise it as an optional feature for goodluckOS.

## AI Contribution Policy
Contributors should feel free to use any tool at their disposal to write their code. Good code is good code regardless who (or what) wrote it. However, large systems that have not been properly tested, documented, or are not understood by the human contributors will _not_ be accepted until all of these conditions are met. This rule applies to both human written code and LLM-generated code.

## LICENSE
This project is licensed under the GNU GENERAL PUBLIC LICENSE VERSION 2.0 see the [LICENSE](LICENSE) file for details.

# DISCLAIMER
**WARNING**: The GNU General Public License v2 covers this in more detail, but to re-iterate: This software has the potential to permanently damage your hardware. You alone are responsible for _all_ damages, and the ensuing results of said damages that may occur as a result of using, or attempting to use this software.
