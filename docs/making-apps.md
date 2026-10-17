# Making apps for goodluckOS

An app is **a folder with an `app.puppy` file in it**. To share it, zip the folder.

Players copy the zip into the `apps` folder on HOME (over USB or with the card in a PC), open
**System > App Installer** and install it. The app then shows up in the launcher's **Apps** row.

```
HelloWorld.zip
└── HelloWorld/           the app's folder; its name is the app's identity
    ├── app.puppy         what the launcher shows and runs (required)
    ├── icon.png          its picture in the launcher
    ├── hello.sh          your program: a script or an ARMv7 binary
    └── data/             what the app writes: kept when the app is updated
```

A ready-made example is in [`example-app/HelloWorld`](example-app/HelloWorld): copy it, rename it, change it.

## app.puppy

The same format as the launcher's own catalog (`/usr/share/puppy/apps.puppy`), with one entry:

```ini
[ENTRY]
NAME=Hello World
DESCRIPTION=A tiny example app
COMMAND=sh hello.sh
ICON=icon.png
VERSION=1.0
TERMINAL=1
```

| Key | | |
|---|---|---|
| `NAME` | required | Shown in the launcher and the installer. |
| `COMMAND` | required | Run with the app's folder as the working directory, as the user `player`, through `sh -c`. |
| `DESCRIPTION` | | One line, shown under the launcher tile. |
| `ICON` | | A PNG in the app's folder, at most 1024 × 1024. Up to 192 × 128 it is drawn at its own size in the middle of the tile (48 × 48 matches the system icons); bigger pictures are scaled to cover the tile, like box art. |
| `VERSION` | | Shown by the installer. |
| `TERMINAL` | | Any value: a text program, run on the console instead of the screen. |

`CATEGORY` is ignored: installed apps always go in the Apps row. Lines starting with `#` are comments.

## The program

- **Scripts** work as they are (`COMMAND=sh run.sh`). HOME is a FAT partition mounted so that every
  file on it can be run, so file permissions in the zip don't matter.
- **Binaries** must be built for the console: ARMv7 hard-float (Cortex-A7 with NEON), linked against
  goodluckOS's own libraries. Use the cross compiler from the rootfs build
  (`output/host/bin/arm-buildroot-linux-*-gcc`); binaries made for other systems may need libraries or
  a C library version goodluckOS doesn't have. Available: SDL2, SDL2_image, SDL2_ttf, OpenGL ES 2 / EGL
  (Mesa lima), ALSA, zlib, libpng, libjpeg, Dear ImGui (`libimgui.so`). Ship anything else your
  program needs in its folder.
- The screen is 640 × 480, the controls are a gamepad. SDL2 apps get them as a game controller.
  `START + SELECT` closes any app, so you don't need a quit button.

## Saves and settings: `data/`

Write everything your app creates (saves, settings, high scores) inside `data/` in its folder.
Installing a new version replaces the app's folder with the zip's contents **except `data/`**, which
moves over to the new version. A `data/` folder in the zip is only used the first time the app is
installed (so it can hold default settings). Uninstalling deletes the whole folder, `data/` included.

## Zipping it

Zip the app's **folder**, so the zip holds `HelloWorld/app.puppy`:

- **Windows:** right-click the folder > *Send to* > *Compressed (zipped) folder*.
- **macOS:** right-click the folder > *Compress*. (The `__MACOSX` and `.DS_Store` files macOS adds
  are ignored.)
- **Linux:** `zip -r HelloWorld.zip HelloWorld`

A zip with `app.puppy` at its root instead also works: the folder is then named after `NAME`.

The installer takes plain zips (Deflate or stored), up to 4 GB. It refuses, before writing
anything: password-protected zips, other compression methods (7-Zip's LZMA, Deflate64), symlinks
(zip the files they point to), and file names that leave the app's folder or that the card can't
store (`\ : * ? " < > |`).

## Installing, updating, uninstalling

- **Install:** the zip is unpacked into a hidden folder, every file is checked against its CRC, and
  only then does it become `apps/<Folder>`. The zip is removed afterwards. There has to be room for
  the unpacked app plus 8 MB.
- **Update:** install a zip whose folder has the same name. The old version is set aside until the
  new one is in place, and `data/` carries over. A power cut at any point is put right the next
  time the installer opens: you get either the old version or the new one, never half of one, and
  the data stays.
- **Uninstall:** in the installer's *Installed Apps* tab, or delete the folder on a PC.
