## [UNRELEASED]
### Added
- USB networking: SSH, SCP and SFTP over the USB cable, switched on with `usb-mode.sh network`
- Luajit + several lua libraries (lsqlite3, lua-sdl2, etc)
- `sqlite3`
- `app-cache/libappcachedb.lua` - A new system application which scans the fs and builds a sqlite3 database containing all applications and metadata.
- `/var/lib/goodluckos/app-cache/locations/` - A directory containing files which contain wildcard-enabled filepaths to files that `app-cache` will scan.
- `/opt/goodluckos/app-cache/scanners/` - A directory of lua sciprts are passed the filepaths found in `app-cache/locations/` and parse those files for application entries.
- `menui` An immediate-mode GUI lua libary that makes it extraordinarily easy to make menu-based applications in Lua. 
- `settings` - A terminal application for viewing and setting system settings available in `libsettings.lua`
- `settings-gui` A user-facing GUI application build with `menui` for changing system settings via `libsettings.lua`
- `integration-tests/` Launches goodluckOS with a QEMU emulator and runs tests via pytest through a serial console.
- `emulator/` A complete goodluckOS emulator. Hardware accelerated graphics, web noVNC frontend.
- `dsperate` A fast NDS emulator
- `pico-puppy` A barebones launcher built with `menui`
- `initial-setup` It's `glos-setup` but written with `menui`
- `simpql.lua` A sqlite3 database generator - automatically generates schema/tables/views/triggers from a simple description.

### Fixed
- Shutdown and reboot no longer hang with the USB cable plugged in

### Changed
- Updated `rootfs/scripts/` to keep downloaded packages and ccaches.
- `kernel/` now uses .config files for kernel config flags instead of hardcoding them into the Dockerfile.
- Puppy has been rewritten in Lua and now lives in `/usr/local/bin/puppy`.
- Puppy now lazy-loads application entries from the app-cache sqlite3 database file.
- Puppy can now scale to any screen resolution.
- Puppy now pins `Terminal` and `System` app category rows to the bottom of the list.
- Puppy now supports `Favourites`, displaying that row at the top of the list.
- `appctl` now finds the `autolaunch` application from the app-cache sqlite3 database file instead of `/home/player/autolaunch`.
- `appctl` now finds the current launcher from the app-cache sqlite3 database file instead of `/home/player/.local/launcher`.
- Moved fonts into subdirs. Done both to improve organization, but also force app developers to use fontconfig (once that gets added).
- Moved ttyGS0's getty command into it's own script so we can check to see if GS0 even exists. This prevents terminal spam on devices whithout ttyGS0.
- Rewrote `appctl` in Lua to make it actually readable and maintainable.
- Launchers should now be given `.puppy` entries, as they can be correctly recognized by the app scanner as launchers.

## [0.1.0]  - 2026-10-10
### Added
- USB MTP
- TF-2 SUPPORT
- A23 Support
- Panel 2 Support
- Appd/Appctl for much better application management
- Evsieve for interacting with TUI/keyboard applications with the gamepad
- Several terminal applications accessible via Puppy
- Repeat-keys in Puppy
- Credits application
- 

### Changed
- Coupled Puppy's application launch behaviour to appctl instead of the OS itself.
- Fixed some button behaviour in Retroarch
- 

## [0.0.1] - 2026-09-26
- Initial release
