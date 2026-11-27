# Puppy architecture

Puppy builds one executable from four internal static libraries.

| Library | Responsibility |
| --- | --- |
| `libpuppy-catalog.a` | Catalogue parsing, ROM discovery, entry data, and navigation state. It has no SDL dependency. |
| `libpuppy-platform.a` | Application launch through `appctl`, cursor and autostart persistence, and battery reads. It shares the catalogue model and shell quoting. |
| `libpuppy-presentation.a` | SDL rendering, icon and font resources, and translation of keyboard and gamepad input into actions. It reads the catalogue model and base palette. |
| `libpuppy-palette.a` | The original Puppy colors used by the renderer. |

`src/main.cpp` coordinates the event loop and connects the libraries.
Each component keeps its header beside its implementation.

## Base palette

`src/palette/palette.h` provides the six original SDL color constants. The
RGBA values live in `src/palette/palette.cpp`. The renderer decides where to
apply each color. Layout, fonts, textures, and drawing remain in
`src/ui/ui.cpp`. The palette is fixed at build time.

The `are-you-sure` confirmation dialog builds the same palette source into
its own executable. It uses the base colors for its background, text,
buttons, and controller focus.
When the prompt opens, **Cancel** receives the initial navigation focus.
The dialog renders the startup frames before waiting indefinitely for
input so the focus highlight can appear without a button press.

## Build and test

From this package directory, use `./build-local.sh` for a native build.
It requires a C++17 compiler, GNU Make, and the development libraries for
SDL2, SDL2_image, and SDL2_ttf. Run `make test` to check catalogue
overrides, ROM filtering, navigation, shell quoting, and input mapping
without a display.

Buildroot uses the same Makefile with its target compiler and archiver and
installs the resulting executable at `/usr/bin/puppy`. The `build.sh`
helper can also use an existing Buildroot toolchain.
