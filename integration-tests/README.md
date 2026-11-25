# goodluckOS Test Suite
This folder contains everything needed to write and run integrations tests against goodluckOS.

It works by starting goodluckOS with QEMU, and automatically running tests via the shell with Python.
This allows us to validate behaviour within the real userspace, catching errors and regressions early, before our users do.

You must build the Kernel and Rootfs prior to running any tests. 
The QEMU emulator will always boot with the latest versions of whatever you've got in:
- `kernel/out/zImage.emulator`
- `rootfs/out/images/rootfs.tar`

    ./start.sh              # build image, convert rootfs.tar -> ext4, boot in QEMU, run pytest
    ./start.sh console      # start an interactive, headless goodluckOS in a QEMU emualator
    ./start.sh -k app_cache -s
