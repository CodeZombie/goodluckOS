################################################################################
#
# app-installer
#
################################################################################

APP_INSTALLER_SITE = $(APP_INSTALLER_PKGDIR)
APP_INSTALLER_SITE_METHOD = local
APP_INSTALLER_DEPENDENCIES = sdl2 sdl2_image imgui zlib

define APP_INSTALLER_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++17 \
		-I$(STAGING_DIR)/usr/include/SDL2 \
		-o $(@D)/app-installer $(@D)/app-installer.cpp \
		$(TARGET_LDFLAGS) \
		-lSDL2 -lSDL2_image -limgui -lz
endef

define APP_INSTALLER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/app-installer $(TARGET_DIR)/usr/bin/app-installer
endef

$(eval $(generic-package))
