# syntax=docker/dockerfile:1

# ==========================================
# BASE: toolchain + kernel source (rarely changes)
# ==========================================
FROM debian:bookworm-slim AS base

RUN apt-get update && apt-get install -y \
    build-essential gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf \
    bc bison flex libssl-dev make libc6-dev libncurses5-dev \
    git wget cpio kmod rsync device-tree-compiler mkbootimg \
    && rm -rf /var/lib/apt/lists/*

ENV ARCH=arm
ENV CROSS_COMPILE=arm-linux-gnueabihf-

WORKDIR /src
RUN git clone --depth=1 -b v7.2.9 https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git

WORKDIR /src/linux


# ==========================================
# HARDWARE KERNEL + DTBs + BOOT IMAGES
# ==========================================
FROM base AS hw_build

# Patches and drivers first: they change least often
COPY patches/ /src/patches/
RUN git apply /src/patches/*.patch

COPY drivers/jd9366-ga36mbv1-2.c drivers/gpu/drm/panel/jd9366-ga36mbv1-2.c
RUN echo 'obj-$(CONFIG_DRM_PANEL_JD9366) += jd9366-ga36mbv1-2.o' >> drivers/gpu/drm/panel/Makefile
RUN sed -i '/^endmenu/i \
config DRM_PANEL_JD9366\n\
	tristate "JD9366 panel"\n\
	depends on OF && DRM && BACKLIGHT_CLASS_DEVICE\n\
	help\n\
	  Say Y here if you want to enable support for JD9366 panels.\n\
' drivers/gpu/drm/panel/Kconfig

COPY drivers/ga36mb-gamepad.c drivers/input/joystick/ga36mb-gamepad.c
RUN echo 'obj-$(CONFIG_GA36MB_GAMEPAD) += ga36mb-gamepad.o' >> drivers/input/joystick/Makefile
RUN sed -i '/^endif/i \
config GA36MB_GAMEPAD\n\
	tristate "GA36MB GAMEPAD"\n\
	depends on SERIAL_DEV_BUS\n\
	help\n\
	  Say Y here to enable support for the GA36-MB Gamepad.\n\
' drivers/input/joystick/Kconfig

# Only the config fragments this stage uses (emulator config changes won't invalidate us)
COPY config/kernel_flags.common.config config/kernel_flags.hardware.config /src/config/

# Configure (after drivers so their Kconfig symbols exist) and build the kernel image
RUN make sunxi_defconfig && \
    ./scripts/kconfig/merge_config.sh -m .config \
        /src/config/kernel_flags.common.config \
        /src/config/kernel_flags.hardware.config && \
    make olddefconfig && \
    make -j$(nproc) zImage

# DTS last: editing a device tree only re-runs this layer, not the kernel build
COPY dts/ arch/arm/boot/dts/allwinner/
RUN echo 'dtb-$(CONFIG_MACH_SUN8I) += ga36mb-a23-panel1.dtb ga36mb-a23-panel2.dtb ga36mb-a33-panel1.dtb ga36mb-a33-panel2.dtb\n' >> arch/arm/boot/dts/allwinner/Makefile
RUN make -j$(nproc) dtbs

# Append DTBs and generate Android boot images
RUN mkdir -p /out/dtb && touch empty_ramdisk && \
    for v in a23_panel1 a23_panel2 a33_panel1 a33_panel2; do \
        dtb="arch/arm/boot/dts/allwinner/ga36mb-$(echo $v | tr _ -)".dtb; \
        cat arch/arm/boot/zImage "$dtb" > "arch/arm/boot/zImage_with_dtb_$v"; \
        mkbootimg --kernel "arch/arm/boot/zImage_with_dtb_$v" --ramdisk empty_ramdisk \
            --base 0x40000000 --board sun8i --pagesize 2048 -o "/out/android_boot_$v.img"; \
        cp "$dtb" /out/dtb/; \
    done && \
    cp .config /out/kernel_config.txt


# ==========================================
# QEMU EMULATOR KERNEL (independent of hw_build)
# ==========================================
FROM base AS qemu_build

COPY patches/ /src/patches/
RUN git apply /src/patches/0006-add-argb8888-to-virtio-gpu-formats.patch

COPY config/kernel_flags.common.config config/kernel_flags.emulator.config /src/config/

RUN make multi_v7_defconfig && \
    ./scripts/kconfig/merge_config.sh -m .config \
        /src/config/kernel_flags.common.config \
        /src/config/kernel_flags.emulator.config && \
    make olddefconfig && \
    make -j$(nproc) zImage

RUN mkdir -p /out && \
    cp arch/arm/boot/zImage /out/zImage.qemu && \
    cp .config /out/qemu_kernel_config.txt


# ==========================================
# ARTIFACT EXPORT TARGETS
# ==========================================
FROM scratch AS artifacts-hw
COPY --from=hw_build /out/ /

FROM scratch AS artifacts-qemu
COPY --from=qemu_build /out/ /

FROM scratch AS artifacts
COPY --from=hw_build /out/ /
COPY --from=qemu_build /out/ /
