################################################################################
#
# ppsspp (libretro)
#
################################################################################

PPSSPP_VERSION = v1.20.4
PPSSPP_SITE = https://github.com/hrydgard/ppsspp.git
PPSSPP_SITE_METHOD = git
# ffmpeg, glslang, SPIRV-Cross, zstd, etc. live in submodules
PPSSPP_GIT_SUBMODULES = YES
PPSSPP_LICENSE = GPL-2.0+
PPSSPP_LICENSE_FILES = LICENSE.TXT

PPSSPP_SUPPORTS_IN_SOURCE_BUILD = NO

PPSSPP_DEPENDENCIES = zlib libgles

PPSSPP_CONF_OPTS = \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
	-DLIBRETRO=ON \
	-DUSING_GLES2=ON \
	-DUSING_EGL=OFF \
	-DUSING_X11_VULKAN=OFF \
	-DUSE_WAYLAND_WSI=OFF \
	-DVULKAN=OFF \
	-DUSE_DISCORD=OFF \
	-DUSE_MINIUPNPC=OFF \
	-DUSE_FFMPEG=ON \
	-DUSE_SYSTEM_FFMPEG=OFF \
	-DUNITTEST=OFF \
	-DHEADLESS=OFF \
	-DSIMULATOR=OFF

ifeq ($(BR2_arm),y)
PPSSPP_CONF_OPTS += -DARM=ON -DARMV7=ON
endif

PPSSPP_CONF_OPTS += -DCMAKE_BUILD_TYPE=Release

# idk where the .so actually lands after it builds, so this finds it and copies it to the target dir :-)
define PPSSPP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 \
		$$(find $(PPSSPP_BUILDDIR) -name 'ppsspp_libretro.so' | head -n1) \
		$(TARGET_DIR)/usr/lib/libretro/ppsspp_libretro.so
	mkdir -p $(TARGET_DIR)/usr/share/libretro/system/PPSSPP
	cp -a $(@D)/assets/. $(TARGET_DIR)/usr/share/libretro/system/PPSSPP/
endef

$(eval $(cmake-package))
