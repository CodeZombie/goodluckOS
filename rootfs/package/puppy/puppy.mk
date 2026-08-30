################################################################################
#
# puppy
#
################################################################################

PUPPY_SITE = $(PUPPY_PKGDIR)
PUPPY_SITE_METHOD = local

PUPPY_DEPENDENCIES = sdl2 sdl2_image sdl2_ttf sdl2_gfx

define PUPPY_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) \
		-o $(@D)/puppy $(@D)/puppy.cpp \
		$(TARGET_LDFLAGS) \
		-lpthread -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_gfx
endef

define PUPPY_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/puppy $(TARGET_DIR)/usr/bin/puppy
endef

$(eval $(generic-package))
