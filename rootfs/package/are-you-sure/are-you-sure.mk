################################################################################
#
# are-you-sure
#
################################################################################

ARE_YOU_SURE_SITE = $(ARE_YOU_SURE_PKGDIR)
ARE_YOU_SURE_SITE_METHOD = local
ARE_YOU_SURE_DEPENDENCIES = sdl2 imgui

define ARE_YOU_SURE_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++11 \
		-I$(@D)/imgui \
		-I$(@D)/imgui/backends \
		-I$(STAGING_DIR)/usr/include/SDL2 \
		-o $(@D)/are-you-sure $(@D)/are-you-sure.cpp \
		$(TARGET_LDFLAGS) \
		-lpthread -ldrm -lSDL2 -lGLESv2 -lEGL -lasound -limgui
endef

define ARE_YOU_SURE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/are-you-sure $(TARGET_DIR)/usr/bin/are-you-sure
endef

$(eval $(generic-package))
