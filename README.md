# goodluckOS
An operating system for victims of fraud.

## What Is This?
GoodluckOS is a custom GNU/Linux firmware for the GA36-MB handheld emulator. It's been built from scratch using a novel containerized buildroot, some custom C/C++/SDL2 userspace software, a few reverse-engineered kernel drivers, and a meticulously crafted custom device tree.

The result is a highly efficient, optimized, and modern linux 7.2 kernel running highly optimized software, specifically chosen to give users enough functionality to play all the games they want, but without any unecessary slowdowns or bloat.

## What Is A GA36-MB?
The r36s is an extremely popular, cheap, and fairly powerful handheld gaming console. It runs linux, it has a massive open source community supporting it, it has thousands of games ported to it, along with thousands more available through emulators. It's like 40 bucks. If you want to play games and don't want to spend much, it's the obvious choice, and a lot of people buy it.

As a result, unscrupulous scammers have decided to flood various marketplaces with knockoffs (or "fakes" as they've come to be known in the r36s community). These fakes are often visually indistinguishable from the a genuine device, and feature significantly worse hardware and software support.

One such fake is the GA36-MB. 

Instead of a RK3326 SoC and 1gb of ram, the GA36-MB uses an Allwinner A33 (or A23 in some cases) and 512mb of ram. You'd have a hard time detecting this, as the manufacturers have gone so far as to etch the official rockchip rk3326 markings onto the a33 chip. They've done the same on the ram as well. The device ships with a copy of Emuelec 4.7 masquerading as the community-favourite ArkOS, and system utils have even been modified to falsely report 1gb of ram when fetching system information.

The stock OS has many, many problems, and should be avoided at all costs. Most critically is the existence of an sd-card-mounted swap volume, which will prematurely kill any SD card you boot from.

## How Do I Install It?
1. Download the latest `goodluckOS.zip` from the [https://releases.com[(Releases section on github)
2. Extract it, 
3. Flash it to a fresh microSD card of at least 1gb capacity.
4. Plug the micro SD card into TF Slot 1 (TF1-OS) on your GA36-MB 
5. Power it on.

## How Do I Build It?
GoodluckOS uses a novel four-step containerized build system featuring Buildroot. This means the OS can be configured and built extremely easily and portably, with no extra dependencies at all besides Docker and Docker Compose.

Take a look at [BUILD.MD](BUILD.md) for instructions and guidelines.

## LICENSE
This project is licensed under the GNU GENERAL PUBLIC LICENSE VERSION 2.0 see the [LICENSE](LICENSE) file for details.

## DISLAIMER
**WARNING**: The GNU General Public License v2 covers this in more detail, but to re-iterate: This software has the potential to permanently damage your hardware. You alone are responsible for _all_ damages, and the ensuing results of said damages, that may occur as a result of using, or attempting to use this software.
