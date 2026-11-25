def test_arch(rootfs):
    assert rootfs.check("uname -m").startswith("armv7")


def test_init_scripts_ran(rootfs):
    # Replace with something your init.d setup actually creates.
    assert rootfs.execute("test -d /etc/init.d").ok
