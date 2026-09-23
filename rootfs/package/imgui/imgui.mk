################################################################################
#
# imgui
#
################################################################################

IMGUI_VERSION = v1.92.9b
IMGUI_SITE = $(call github,ocornut,imgui,$(IMGUI_VERSION))
IMGUI_LICENSE = MIT
IMGUI_LICENSE_FILES = LICENSE.txt

# This package must be installed to staging so other packages can find its headers and .so file
IMGUI_INSTALL_STAGING = YES
IMGUI_DEPENDENCIES = sdl2

# C++11 minimum.
IMGUI_CXXFLAGS = $(TARGET_CXXFLAGS) -std=c++11 -fPIC -I$(@D)

# The core files and SDL2 backend files we need to compile
IMGUI_SRCS = \
	imgui.cpp \
	imgui_draw.cpp \
	imgui_tables.cpp \
	imgui_widgets.cpp \
	backends/imgui_impl_sdl2.cpp \
	backends/imgui_impl_sdlrenderer2.cpp

# Compile each .cpp into a .o, then link them into libimgui.so
define IMGUI_BUILD_CMDS
	$(foreach src,$(IMGUI_SRCS),\
		$(TARGET_CXX) $(IMGUI_CXXFLAGS) -I$(STAGING_DIR)/usr/include/SDL2 -c $(@D)/$(src) -o $(@D)/$(notdir $(src:.cpp=.o))
	)
	$(TARGET_CXX) $(TARGET_LDFLAGS) -I$(STAGING_DIR)/usr/include/SDL2 -shared -o $(@D)/libimgui.so $(@D)/*.o -lSDL2
endef

# install the library and necessary headers to STAGING_DIR for other apps to compile against
define IMGUI_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 0755 $(@D)/libimgui.so $(STAGING_DIR)/usr/lib/libimgui.so

	# Core headers
	$(INSTALL) -D -m 0644 $(@D)/imgui.h $(STAGING_DIR)/usr/include/imgui.h
	$(INSTALL) -D -m 0644 $(@D)/imconfig.h $(STAGING_DIR)/usr/include/imconfig.h
	$(INSTALL) -D -m 0644 $(@D)/imgui_internal.h $(STAGING_DIR)/usr/include/imgui_internal.h
	$(INSTALL) -D -m 0644 $(@D)/imstb_rectpack.h $(STAGING_DIR)/usr/include/imstb_rectpack.h
	$(INSTALL) -D -m 0644 $(@D)/imstb_textedit.h $(STAGING_DIR)/usr/include/imstb_textedit.h
	$(INSTALL) -D -m 0644 $(@D)/imstb_truetype.h $(STAGING_DIR)/usr/include/imstb_truetype.h

	# Backend headers
	$(INSTALL) -D -m 0644 $(@D)/backends/imgui_impl_sdl2.h $(STAGING_DIR)/usr/include/imgui_impl_sdl2.h
	$(INSTALL) -D -m 0644 $(@D)/backends/imgui_impl_sdlrenderer2.h $(STAGING_DIR)/usr/include/imgui_impl_sdlrenderer2.h
endef

# Install only the compiled shared library to the TARGET_DIR
define IMGUI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/libimgui.so $(TARGET_DIR)/usr/lib/libimgui.so
endef

$(eval $(generic-package))
