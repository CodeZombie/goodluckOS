################################################################################
#
# system-settings
#
################################################################################

SYSTEM_SETTINGS_SITE = $(SYSTEM_SETTINGS_PKGDIR)
SYSTEM_SETTINGS_SITE_METHOD = local
SYSTEM_SETTINGS_DEPENDENCIES = sdl2 imgui

define SYSTEM_SETTINGS_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++11 \
		-I$(STAGING_DIR)/usr/include/SDL2 \
		-o $(@D)/system-settings $(@D)/system-settings.cpp \
		$(TARGET_LDFLAGS) \
		-lpthread -ldrm -lSDL2 -lGLESv2 -lEGL -lasound -limgui
endef

define SYSTEM_SETTINGS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/system-settings $(TARGET_DIR)/usr/bin/system-settings
endef

$(eval $(generic-package))
