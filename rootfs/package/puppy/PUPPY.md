# Puppy
**Puppy** is the default application launcher for **goodluckOS**. It was built from scratch to be small, simple, flexible, and most importantly: fast.

Puppy works alongside `appd` to ensure that it never hogs system resources while you play. When you launch a game or application, Puppy completely closes out, leaving all RAM and CPU power dedicated entirely to your game.

---

## Features
- Written in C++ using SDL2 for lightweight rendering and instant startup.
- Completely exits upon launching games/apps to free up system memory and processing power.
- Modern and familiar directional navigation across categories and application tiles.
- Automatically expands archive directories into launchable tiles based on defined extensions and Libretro cores.
- Easily set any application to launch automatically at boot.

---

## File Structure & System Paths
Puppy looks for configuration files, standard desktop applications, and system state in the following locations:

| Path | Description |
| :--- | :--- |
| `/usr/share/puppy/apps.puppy` | System-wide application definitions |
| `/home/player/apps.puppy` | User application definitions |
| `/home/player/.local/share/applications/*.puppy` | Modular user `.puppy` configuration files |
| `/home/player/autolaunch` | Target configuration file for autolaunching on boot |

---

## Configuration (`apps.puppy`) Syntax
Puppy uses INI-formatted `.puppy` files to populate its application catalog. 

### Direct Application Entries (`[ENTRY]`)
To add an individual application or game, use the `[ENTRY]` block:

```ini
[ENTRY]
CATEGORY=Games
NAME=SuperTux
DESCRIPTION=A classic 2D jump 'n run sidescroller game.
COMMAND=/home/player/ports/supertux/supertux.sh
ICON=/home/player/ports/supertux/icon.png
TERMINAL=0
```

### Archive Entries (`[ARCHIVE]`)
To automatically scan a directory for ROMs or software and map them to an emulator core:

```ini
[ARCHIVE]
NAME=Game Boy Advance
DESCRIPTION=GBA Games via RetroArch
COMMAND=retroarch -L /usr/lib/libretro/mgba_libretro.so %s
DEFAULT_ICON=/usr/share/puppy/assets/gba_icon.png
ENTRY_DIRECTORIES=/home/player/roms/gba;
ENTRY_EXTENSIONS=gba;zip;7z;
ENTRY_ICONS_DIRECTORIES=/home/player/roms/gba/icons;
```

---

## Controls
| Action | Handheld Control |
| :--- | :--- |
| **Navigate Grid** | D-Pad / Left Analog Stick |
| **Launch App** | **A** Button |
| **Toggle Autolaunch** | **Y** Button |

---

## Frequently Asked Questions
### Can I add my own emulator cores / archives?
Yes! To add a new emulator:
1. Obtain the appropriate `armhf` Libretro core `.so` file.
2. Place it into `/usr/lib/libretro/` or anywhere in your `HOME` partition.
3. Open `/home/player/apps.puppy` and add a new `[ARCHIVE]` section pointing `COMMAND` to your core binary, using `%s` as the filename placeholder.

---

## Coming Soon
- EmulationStation (`es_systems.cfg`) schema support
- Custom themes and layout customization
