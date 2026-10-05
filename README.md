# goodluckOS
An uncompromisingly fast, small, modern, and feature-rich custom firmware for A23/A33-based handheld consoles.

## PLEASE READ
goodluckOS is PRE-RELEASE software. There are no gaurantees that it will work on your hardware.

Please visit [The Firmware Builder](https://codezombie.github.io/goodluckOS/download.html). This will walk you through the steps of identifying if you have a compatible unit, and if so, easily building a bootable goodluckOS image.

Please Note: At the time of writing, Allwinner-A23 based GA36-MB devices (which seem to be what most people have) are _not_ supported.

## Features
- Mainline Linux 7.2
- Everything compiled from scratch with the best optimization flags for the hardware
- Optimized for performance. No systemd, no unecessary background processes, no x11/wayland
- Significantly smaller and faster than the stock firmware
- Boots in 10 seconds (stock firmware takes 50)
- Can be flashed to a 1gb SD card and still give you over 500mb of free space for games
- ZRAM enabled by default
- Hardware accelerated graphics
- Speaker audio
- Great battery life
- No swap on SD card (massively improves the life of your card over the stock f/w)
- Built-in `Resize Home` app which grows your HOME partition to fill all the available space on your microSD card.
- FN+Vol buttons adjust the screen brightness from anywhere
- FN+START+SELECT kills the active application, bringing you right back to the launcher
- FN+DPAD_UP toggles a live FPS/CPU graph for monitoring in-game performance.
- Headphones work. FN+DPAD_DOWN switches the sound between the speaker and the headphones, since the GA36-MB v1.1 can't detect the jack (the speaker comes back on at boot)
- If the screen is on, the LEDs are off. Nothing blinding you while you're playing in the dark
- Comes stock with Chocolate Doom, ready to play
- Comes stock with Retroarch and several optimized cores (PCSX-ReArmed, Snes9x, QuickNES, DOSbox, Genesis Plus GX, mGBA, etc)
- Games perform very well. Metal Gear Solid 1 is completely playable at reasonable framerates
- Comes with a custom, optimized launcher application
- Autostart any application on boot, including games or a front-end like EmulationStation (coming soon)
- USB terminal access for remote debugging. Log in with `sudo screen /dev/ttyACM* 115200` and `root:root`
- HOME shows up on your PC over USB (MTP), like a phone: copy games and saves without taking the SD card out
- Half Life 1 ported and playable: [Get It Here](https://github.com/CodeZombie/glOSports-half-life)

### Features In Development
- Portmaster (or equivalent)
- EmulationStation
- USB Networking
- Headphone support
- GA36-MB TF-2 support (If the hardware supports it)
- CPU overclocking
- Improve performance of PPSSPP emulator core. 

## Supported Devices
- GA36-MB v1.2
- GA36-MB v1.1

#### Coming Soon:
- GA36-MB v1.0
- F35v
- Other A23/A33-based devices

Know of another Allwinner-based handheld not listed on this page? Open a new [Issue](https://github.com/CodeZombie/goodluckOS/issues) and let me know!

Consider contributing to the [Device Fund](https://ko-fi.com/jeremyclark) so I can purchase new consoles and port goodluckOS to them.

## Puppy
Puppy is goodluckOS' application launcher. It's what you see when you start goodluckOS.

It starts up fast, uses very little power, launches applications instantly, and uses zero RAM and CPU after launching an application.

Puppy uses a ini-formatted `apps.puppy` files to populate it's application list. 

Add a new entry by modifying the `apps.puppy` file in your HOME partition. Take a look at the comments at the top of that file for syntax/examples

## What's wrong with the stock firmware?
Stock firmware:
- Based on ancient, unsupported forks of thelinux kernel version 3.4.
- Messy file system, hard to customize, even harder to clean.
- Full of broken/incompatible files.
- Come with inefficient, out-of-date emulator cores.
- Software often not compiled with optimal compiler flags.
- Usually uses swap-on-sd card, which will prematurely kill your expensive microSD card.
- Slow boot time
- Bad/no hotkeys
- Can't be flashed to smaller SD cards.
- The MBR (at least on the GA36-MB) is broken by default. You can't add games unless you manually fix it with my [guide](https://gist.github.com/CodeZombie/83be58b000ee6a14c7b91a6027a8eedf)

## How Do I Install goodluckOS?
1. Download the latest `goodluckOS.zip` from [Releaases](https://github.com/CodeZombie/goodluckOS/releases)
2. Extract it
3. Flash it to a microSD card of at least 1gb capacity with [balenaEtcher](https://etcher.balena.io/), [Rufus (in DD mode)](https://rufus.ie/en/), [dd](https://man7.org/linux/man-pages/man1/dd.1.html), etc
4. Plug the micro SD card into TF Slot 1 (TF1-OS) on your GA36-MB
5. Power it on.
6. [optional] Select the `Resize Home` application in the launcher to expand your HOME partition to fill all the remaining space on your SD card. You only need to do this once.

## How do I add games?
Plug the console into your PC with a USB cable and open `GA36MB -> HOME` (Windows: This PC; Linux: your file manager; macOS needs an MTP app such as [OpenMTP](https://openmtp.ganeshrvel.com/)). Or plug the SD card into your PC and open up the HOME partition. In there you'll find a `roms` folder with a few subfolders for each system. Add your roms to those.

If the console was connected to a charger when it booted (no PC), restart it before plugging it into a PC to get MTP; the serial console works either way. Games copied while Puppy is open show up after you reopen it.

To add custom art to Puppy, add an image file into the `icons` folder in your rom folder with the same name as the rom file. (eg. if your game is `roms/snes/super-mario.smc`, your image would be `roms/snes/icons/super-mario.png`)

### Can I add my own archives?
You sure can!

To add a new emulator, find an appropriate armhf libretro core .so file. Add it to either:
1. The `/usr/lib/libretro` folder in the `linux` partition, or
2. Anywhere in your HOME partition

Then modify `HOME -> apps.puppy` with a new `[ARCHIVE]` entry, pointing the COMMAND to your new libretro core. Take a look at `HOME -> apps.puppy` for an exmaple.

## Does goodluckOS come with any games?
Only prBoom with a shareware copy of Doom. However I haven't been able to figure our prBoom's cryptic controller scheme yet so it's not exactly playable, but you can watch the demo :). (If you know how to get this working please make an MR I have spent hours on this and I cannot figure it out and I'm at the end of my rope.)

goodluckOS will never be distributed with unauthorized copyright protected materials. If you own the copyright to any games or demos (or know of any permissively-licensed/CC games) that you think might make a good fit, please open an Issue! I'd love to include some high quality games with the OS by default.

## About Ports
Some subset of portmaster games could theoretically work, however I want to tamper your expectations a little bit before we all start getting too excited.
The Allwinner A23/A33 is a 32-bit SoC. This means that 64-bit software simply _cannot_ run on it. There are a number of popular android ports that exist only as aarch64 (64-bit) distributions. Those will never work. Additionally, even with ZRAM there, the device is still heavily RAM-limited, so unoptimized games (I'm looking at you, Balatro) will struggle to run without patching.

With that said, most of the games people actually care about (Stardew Valley, Half-Life 1, n64 re-comps, Android GameMaker games, OpenMW, OpenRCT2, + more) are 32-bit games, and therefore should theoretically work if anyone cares to create 32-bit ports.
## How Do I Install It?
1. Download the latest `goodluckOS.zip` from [COMING SOON]
2. Extract it
3. Flash it to a microSD card of at least 1gb capacity with [Rufus](https://rufus.ie/en/), [balenaEtcher](https://etcher.balena.io/), [dd](https://man7.org/linux/man-pages/man1/dd.1.html), etc
4. Plug the micro SD card into TF Slot 1 (TF1-OS) on your GA36-MB 
5. Power it on
6. Wait for the auto-resize process to finish (the blue LED will turn on when it starts, and off when it's done) before powering off.

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

I am currently in the process of choosing a package-management solution for goodluckOS, so if your application/feature is not approved for merging upstream, please feel free to package it up as an installable application and I will be happy to advertise it as an optional feature for goodluckOS users.

## AI Contribution Policy
Contributors should feel free to use any tool at their disposal to write their code. Good code is good code regardless who (or what) wrote it. However, large systems that have not been properly tested, documented, or are not understood by the human contributors will _not_ be accepted until all of these conditions are met. This rule applies to both human written code and LLM-generated code.

## LICENSE
This project is licensed under the GNU GENERAL PUBLIC LICENSE VERSION 2.0 see the [LICENSE](LICENSE) file for details.

## DISCLAIMER
**WARNING**: The GNU General Public License v2 covers this in more detail, but to re-iterate: This software has the potential to permanently damage your hardware. You alone are responsible for _all_ damages, and the ensuing results of said damages that may occur as a result of using, or attempting to use this software.
