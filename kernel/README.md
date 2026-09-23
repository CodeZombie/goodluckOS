# goodluckOS Kernel
This is the goodluckOS Linux kernel.

## Drivers
Two drivers have been written via reverse-engineering to support the entirely undocumented Display and Gamepad peripherals.

### The Display
The display was a complete nightmare to figure out. 

I've spent around 100 hours on just this driver, but I think it's finally in a state where I can say it's ship-worthy.

The script.bin file (a primitive device-tree alternative) contained reference to a jd9366_8inch as lcd0. I found a simple jd9366 driver online. It's basically boilerplate MIPI-DSI. I set it all up, compiled, booted, and the display wouldn't initialize. Turn out I needed the specific initialization sequence to get the panel to do anything. There's basically zero specific markings anywhere on the display to indicate what it actually is (I doubt it's an actual jd9366_8inch. The actual display is 3.5 inches).

The `init sequence` was eventually discovered by pulling the original linux 3.4 compiled kernel module (lcd.ko) from the original firmware and analyzing it with Ghidra.
Thankfully the dorks who compiled the original firmware left debug symbols on, so REing was not nearly as difficult as it should have been. Within a few hours I had my init sequence.

Some slight modifications were made to get the jd9366 driver to work on linux 6.12 (and eventually 7.2), I inserted the init sequence and boom: we had pixels.

This got the display up, but only about 90% of the time. The remaining 10% of the time the display would fail to initialize. 

I added debug statements everywhere, and the kernel logs would either tell me the device was offline, or the MIPI d-phy was unavailable.

This was almost certainly the result of the bootloader initializing the display, pushing pixels, and then not doing any kind of cleanup before handing it off the to the linux kernel.
90% of the time the display was in a functional-enough state to allow the kernel to run the probe/prepare in the driver, but other times it wasn't. 

I then spent about four straight days debugging, adding `dev_info` calls to every concevable file in the linux source that touches mipi/dsi/dcs/drm, but nothing gave any kind of indication of why the failure was happening, only that the display was not responding.

So as it turns out: my driver was assuming the device would be powered off by the time it was initialized, it wasn't doing any kind of hardware reset beyond the standard reset-pin toggle, which wasn't enough with the display in it's weird state left over by the bootloader.

I added a fun little power-off-power-on cycle at the top of the `prepare` sequence before finally:it would reliably start initialize every single time.

### The Gamepad
This one was very strange. The manufacturer decided to put the gamepad buttons (except vol+/vol-) straight on GPIOs, super easy. I technically don't even need to write a driver to get those working, I could get linux to recognize them as buttons or keys straight from the `.dts`. However, the analog sticks are wired up in a surprising way. It turns out the Allwinner A33 doesn't really have general purpose ADC pins, so if you want to read analog values from something, say, an analog stick's axis, you have to either hack it into the audio subsystem's mic-in, hack it into the 6-bit LRADC (probably not ideal for reading an anog stick), or (and this is what they did) add a second, low power MCU to read the analog values and transmit them back to the A33 over a bus like SPI or I2C. Bizarrely, they decided to not use SPI or I2C and instead opted for uart. Strange choice.

The annoying part about this is that the secondary MCU requires an initialization sequence from the host (the a33). So once again I found the driver module in the original firmware, cracked open Ghidra and found the bytes.

At this point I had analog sticks working and GPIOs working in the dts. To consolidate everything so it would enumerate as a single device in Linux, I took the GPIOs out of the dts and polled them inside my driver. Retroarch (and probably every other game) don't like using two gamepad controllers at once, so this seemed like the simplest low level solution to that problem.

## How Do I Flash this Directly?
If you want to test changes to the kernel without rebuilding/reflashing the entire goodluckOS image, follow these steps:
1. Run `./build.sh` to build the kernel image.
2. Plug your goodluckOS microSD card into your linux computer
3. Run `lsblk` to identify the sd card device (not a partition)
4. Flash `out/android_boot.img` with `sudo dd if=out/android_boot.img bs=512 seek=172032 conv=notrunc status=progress of=/dev/sdX` where `sdX` is your microSD card device.
5. Run `sync` to make sure the bits are fully written to the microSD card.
