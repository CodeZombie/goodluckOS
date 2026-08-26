################################################################################
#
# system-settings
#
################################################################################

SYSTEM_SETTINGS_SITE = $(SYSTEM_SETTINGS_PKGDIR)
SYSTEM_SETTINGS_SITE_METHOD = local
SYSTEM_SETTINGS_DEPENDENCIES = sdl2

# Compile standard C++ files plus ImGui source files
define SYSTEM_SETTINGS_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++11 \
		-I$(@D)/imgui \
		-I$(@D)/imgui/backends \
		-I$(STAGING_DIR)/usr/include/SDL2 \
		-o $(@D)/system-settings $(@D)/system-settings.cpp \
		$(@D)/imgui/imgui.cpp \
		$(@D)/imgui/imgui_draw.cpp \
		$(@D)/imgui/imgui_tables.cpp \
		$(@D)/imgui/imgui_widgets.cpp \
		$(@D)/imgui/backends/imgui_impl_sdl2.cpp \
		$(@D)/imgui/backends/imgui_impl_sdlrenderer2.cpp \
		$(TARGET_LDFLAGS) \
		-lSDL2
endef

define SYSTEM_SETTINGS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/system-settings $(TARGET_DIR)/usr/bin/system-settings
endef

$(eval $(generic-package))
