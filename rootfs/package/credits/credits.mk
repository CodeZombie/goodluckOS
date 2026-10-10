################################################################################
#
# credits
#
################################################################################

CREDITS_SITE = $(CREDITS_PKGDIR)
CREDITS_SITE_METHOD = local

CREDITS_DEPENDENCIES = sdl2 sdl2_ttf

define CREDITS_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) \
		-o $(@D)/credits $(@D)/credits.cpp \
		$(TARGET_LDFLAGS) \
		-lpthread -lSDL2 -lSDL2_ttf
endef

define CREDITS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/credits $(TARGET_DIR)/usr/bin/credits
endef

$(eval $(generic-package))
