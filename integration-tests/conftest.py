import os

import pytest

from device import QemuDevice


@pytest.fixture(scope="session")
def rootfs():
    dev = QemuDevice(kernel=os.environ["KERNEL_IMG"], rootfs=os.environ["ROOTFS_IMG"])
    dev.start()
    try:
        dev.login()
        yield dev
    finally:
        dev.stop()
