################################################################################
#
# resize-home
#
################################################################################

RESIZE_HOME_SITE = $(RESIZE_HOME_PKGDIR)
RESIZE_HOME_SITE_METHOD = local
RESIZE_HOME_DEPENDENCIES = sdl2 imgui

define RESIZE_HOME_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++17 \
		-I$(STAGING_DIR)/usr/include/SDL2 \
		-o $(@D)/resize-home $(@D)/resize-home.cpp \
		$(TARGET_LDFLAGS) \
		-lpthread -ldrm -lSDL2 -lGLESv2 -lEGL -limgui
endef

define RESIZE_HOME_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/resize-home $(TARGET_DIR)/usr/bin/resize-home
endef

$(eval $(generic-package))
