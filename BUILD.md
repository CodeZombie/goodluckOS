# goodluckOS
A modern, performance-optimized operating system for the GA36-MB.

## Builidng
The build process for goodluckOS is split into four sections: The Kernel, The Rootfs, The Home Overlay, and The Image.
The fist three steps may be executed in any order, but all must be completed before step 4 (The Image) is run.
When working on a section of the OS a full four-step build may not be efficient. The output of any step can be flashed over an existing sd card or sd card img file, so you don't have to build the entire OS image or flash the entire OS every time you make a change.

### 1. The Kernel
The kernel is configured and compiled using Docker buildx (or Podman). Enter the `kernel` directory and run `build.sh`.

The kernel boot image file (android_boot.img) can be flashed manually to an sd card or sd card image by running:
```
sudo dd if=android_boot.img bs=512 seek=172032 conv=notrunc status=progress of=/dev/sdX
```

### 2. The Rootfs
The rootfs is configured and built using a containerized Buildroot. Enter the `rootfs` director and run `start.sh` to build and enter the buildroot-enabled system.
Once inside, the following commands are available:
- `./scripts/menuconfig.sh`: Opens the Buildroot config application to customize the rootfs.
- `./scripts/busybox-menuconfig.sh`: Opens the Busybox config application to customzie busybox.
- `./scripts/configure.sh`: Parses the contents of `./configs/goodluck_defconfig` for the current buildroot session. This script runs automatically upon entering the container.
- `./scripts/clean.sh`: Removes all build artifacts.
- `./scripts/build.sh`: Builds the rootfs. If succesful, a `rootfs.tar` will be placed in the `output` directory.

A built rootfs can be added to an existing goodluckOS SD card by mounting the `linux` partition, deleting everything within it, and un-tarring `rootfs/out/images/rootfs.tar` into the now-empty partition.

#### Custom Applications
Custom applications are stored in the rootfs directory at `rootfs/package/`. 
Create a directory in here with the name of your package. At minimum this will contain two files:
- your-app.mk
- Config.in

The .mk file contains the build instructions, and Config.in contains the configuration options necesary to register the application with Buildroot.

You may include extra files, such as local development scripts, etc. Take a look at `rootfs/package/puppy` for example. It includes a `build-local.sh` so you can modify, build, and test puppy directly on your local machine right in the same location where it will eventually be built for goodluckOS. Just be sure to add any extra artifacts to .gitignore, or ensure any local build artifacts are stored in the package's `out` directory which is automaticaly gitignored by default.

Be sure to include an entry to your custom package in `rootfs/package/Config.in` so Buildroot can find your package's `Config.in` file.

### 3. The Home Overlay
A custom home directory overlay can be created inside the `home/overlay` directory. This will be laid on top of `/home/player` in the final image inside of partition #2 (HOME).
Games, skeleton folder structures, config files that the user may want to modify, and other media should be placed here.
Run `home/build.sh` to bundle the contents of `home/overlay` into `home/out/home.tar`, which will be used by step 4 when creating final image.

Like the rootfs, the home overlay can also be added to any existing goodluckOS sd card by mounting the HOME partition, and deleting every file within the `player` directory, then untarring `home/out/home.tar` into the now empty `player` directory.

### 4. The Image
The final, flashable image can only be generated after the kernel and rootfs have been created.
Prerequisites:
- The Kernel must have been built and exist in `kernel/out/android_boot.img`
- The rootfs must have been built and exist in `rootfs/output/rootfs.tar`
- A dump of an original GA36-MB sd card must exist at `image/sdcard.img`. Only the first 128mb of the sd card file are necessary. See: [Dumping your Micro SD Card](#dumping-your-microsd-card).

Enter the `image` directory and run `build.sh`.

This image can now be flashed directly onto an SD Card and used in TF slot 1 to boot goodluckOS.

## Dumping your MicroSD Card
To build a complete, flashable goodluckOS image, you must provide the first 128mb of data from the original microSD card that came with the GA36-MB.
A complete 64gb dump of the sd card can be found online, or you can dump just the part we need yourself:
1. Plug your microSD card into your linux computer
2. run `lsblk` and identify the device path of the microSD card.
3. run `sudo dd if=/dev/sdX of=sdcard.img bs=1M count=128 status=progress` where `/dev/sdX` is your microSD card.
4. `sdcard.img` now contains everything you need. Copy that to the `image/` folder.
