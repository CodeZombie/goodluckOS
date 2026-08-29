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
	mgba \
	gpsp \
	quicknes \
	mupen64plus_next \
	ppsspp \
	flycast \
	dosbox \
	pcsx_rearmed

define LIBRETRO_CORES_RUN_FETCH
	cd $(@D) && \
		./libretro-fetch.sh $(LIBRETRO_MAJOR_CORES_LIST)
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
endef

define LIBRETRO_CORES_INSTALL_TARGET_CMDS
	$(INSTALL) -d $(TARGET_DIR)/usr/lib/libretro
	find $(@D)/dist/unix -name "*_libretro.so" -exec \
		$(INSTALL) -D -m 0755 {} $(TARGET_DIR)/usr/lib/libretro/ \;
endef

$(eval $(generic-package))
