################################################################################
#
# libretro-cores
#
################################################################################

LIBRETRO_CORES_VERSION = Latest
LIBRETRO_CORES_SITE = https://github.com/libretro/libretro-super.git
LIBRETRO_CORES_SITE_METHOD = git
LIBRETRO_CORES_LICENSE = Various (GPL-2.0+, MIT, Non-Commercial)
LIBRETRO_CORES_LICENSE_FILES = COPYING

LIBRETRO_CORES_DEPENDENCIES = zlib host-cmake

LIBRETRO_MAJOR_CORES_LIST = \
	fceumm \
	snes9x2005 \
	genesis_plus_gx \
	quicknes \
	dosbox \
	pcsx_rearmed \
	gambatte

CMAKE_LIBRETRO_CORES_LIST = \
	mgba


define LIBRETRO_CORES_RUN_FETCH
	cd $(@D) && ./libretro-fetch.sh $(LIBRETRO_MAJOR_CORES_LIST)
	cd $(@D) && ./libretro-fetch.sh $(CMAKE_LIBRETRO_CORES_LIST)
endef
LIBRETRO_CORES_POST_EXTRACT_HOOKS += LIBRETRO_CORES_RUN_FETCH

define LIBRETRO_CORES_BUILD_CMDS
	cd $(@D) && \
		CC="$(TARGET_CC)" \
		CXX="$(TARGET_CXX)" \
		AR="$(TARGET_AR)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		CXXFLAGS="$(TARGET_CXXFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		MAKE="$(MAKE)" \
		./libretro-build.sh $(LIBRETRO_MAJOR_CORES_LIST)

	rm -rf $(@D)/libretro-mgba/build
	mkdir -p $(@D)/libretro-mgba/build
	cd $(@D)/libretro-mgba/build && \
		$(HOST_DIR)/bin/cmake .. \
			-DCMAKE_TOOLCHAIN_FILE=$(HOST_DIR)/share/buildroot/toolchainfile.cmake \
			-DCMAKE_BUILD_TYPE=Release \
			-DBUILD_LIBRETRO=ON \
			-DBUILD_SDL=OFF \
			-DBUILD_QT=OFF \
			-DUSE_FFMPEG=OFF \
			-DUSE_SQLITE3=OFF \
			-DUSE_MINIZIP=OFF \
			-DUSE_EDITLINE=OFF \
			-DUSE_DISCORD_RPC=OFF
	$(MAKE) -C $(@D)/libretro-mgba/build mgba_libretro

	mkdir -p $(@D)/dist/unix
	cp $(@D)/libretro-mgba/build/mgba_libretro.so $(@D)/dist/unix/mgba_libretro.so
endef

define LIBRETRO_CORES_INSTALL_TARGET_CMDS
	$(INSTALL) -d $(TARGET_DIR)/usr/lib/libretro
	find $(@D)/dist/unix -name "*_libretro.so" -exec \
		$(INSTALL) -D -m 0755 {} $(TARGET_DIR)/usr/lib/libretro/ \;
endef

$(eval $(generic-package))
