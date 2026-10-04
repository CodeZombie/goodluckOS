# syntax=docker/dockerfile:1
FROM debian:bookworm-slim AS builder

ENV LIBGUESTFS_BACKEND=direct

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    parted \
    libguestfs-tools \
    qemu-system-x86 \
    linux-image-amd64 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

#######
# A33 #
#######
RUN truncate -s 1G goodluck_a33.img && \
    parted -a optimal -s goodluck_a33.img \
    mklabel msdos \
    mkpart primary ext4 128MiB 512MiB \
    mkpart primary fat32 512MiB 100%

# COPY sdcard.img ./
RUN dd if=/dev/zero of=goodluck_a33.img bs=512 skip=1 seek=1 count=262143 conv=notrunc

COPY staging/android_boot_a33.img ./
RUN dd if=android_boot_a33.img of=goodluck_a33.img bs=512 seek=172032 conv=notrunc

COPY staging/rootfs.tar ./
RUN guestfish -a goodluck_a33.img <<EOF
run
mkfs-opts ext4 /dev/sda1 label:linux
mount /dev/sda1 /
tar-in rootfs.tar /
umount /
EOF

COPY staging/home.tar ./
RUN guestfish -a goodluck_a33.img <<EOF
run
mkfs-opts vfat /dev/sda2 label:HOME
mount /dev/sda2 /
tar-in home.tar /
umount /
EOF

# # Inject custom bootlogo
# COPY res/goodluck_bootlogo.bmp ./goodluck_bootlogo.bmp
# RUN guestfish -a goodluck_a33.img <<EOF
# run
# mount-vfs "offset=37748736" "vfat" /dev/sda /
# upload goodluck_bootlogo.bmp /bootlogo.bmp
# umount /
# EOF

#######
# A23 #
#######
RUN truncate -s 1G goodluck_a23.img && \
    parted -a optimal -s goodluck_a23.img \
    mklabel msdos \
    mkpart primary ext4 128MiB 512MiB \
    mkpart primary fat32 512MiB 100%

# COPY sdcard.img ./
RUN dd if=/dev/zero of=goodluck_a23.img bs=512 skip=1 seek=1 count=262143 conv=notrunc

COPY staging/android_boot_a23.img ./
RUN dd if=android_boot_a23.img of=goodluck_a23.img bs=512 seek=172032 conv=notrunc

COPY staging/rootfs.tar ./
RUN guestfish -a goodluck_a23.img <<EOF
run
mkfs-opts ext4 /dev/sda1 label:linux
mount /dev/sda1 /
tar-in rootfs.tar /
umount /
EOF

COPY staging/home.tar ./
RUN guestfish -a goodluck_a23.img <<EOF
run
mkfs-opts vfat /dev/sda2 label:HOME
mount /dev/sda2 /
tar-in home.tar /
umount /
EOF

# # Inject custom bootlogo
# COPY res/goodluck_bootlogo.bmp ./goodluck_bootlogo.bmp
# RUN guestfish -a goodluck_a23.img <<EOF
# run
# mount-vfs "offset=37748736" "vfat" /dev/sda /
# upload goodluck_bootlogo.bmp /bootlogo.bmp
# umount /
# EOF

FROM scratch AS export
COPY --from=builder /workspace/goodluck_a33.img /goodluck_base_a33.img
COPY --from=builder /workspace/goodluck_a23.img /goodluck_base_a23.img
