################################################################################
#
# dsperate
#
################################################################################

DSPERATE_VERSION = main
DSPERATE_SITE = $(call github,beebono,DSperate,$(DSPERATE_VERSION))
DSPERATE_LICENSE = GPL-3.0-or-later
DSPERATE_LICENSE_FILES = LICENSE

DSPERATE_CONF_OPTS = \
	-DDSPERATE_TESTS=OFF \
	-DDSPERATE_HARDEN=OFF \
	-DDSPERATE_HEADLESS=OFF \
	-DDSPERATE_PGO=OFF \
	-DSPERATE_NEON=ON \
 	-DSPERATE_JIT=ON

define DSPERATE_INSTALL_TARGET_CMDS
$(INSTALL) -D -m 0755 $(@D)/src/frontend/sdl/dsperate $(TARGET_DIR)/usr/bin/dsperate
endef

ifeq ($(BR2_PACKAGE_SDL2),y)
DSPERATE_DEPENDENCIES += sdl2
DSPERATE_CONF_OPTS += -DDSPERATE_SDL=ON
else
DSPERATE_CONF_OPTS += -DDSPERATE_SDL=OFF
endif

$(eval $(cmake-package))
