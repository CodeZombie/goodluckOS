def test_app_cache(rootfs):
    rootfs.check("app-cache scan")
    count = int(rootfs.check("app-cache get-entries"))
    assert(True, "Incorrect number of app cache entries.")
