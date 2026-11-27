################################################################################
#
# puppy
#
################################################################################

PUPPY_SITE = $(PUPPY_PKGDIR)
PUPPY_SITE_METHOD = local

PUPPY_DEPENDENCIES = sdl2 sdl2_image sdl2_ttf

define PUPPY_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) \
		AR="$(TARGET_AR)" \
		CXX="$(TARGET_CXX)" \
		CPPFLAGS="$(TARGET_CPPFLAGS)" \
		CXXFLAGS="$(TARGET_CXXFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		BUILD_DIR="$(@D)/build" OUTPUT="$(@D)/puppy" all
endef

define PUPPY_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/puppy $(TARGET_DIR)/usr/bin/puppy
endef

$(eval $(generic-package))
