################################################################################
#
# gallery
#
################################################################################

GALLERY_SITE = $(GALLERY_PKGDIR)
GALLERY_SITE_METHOD = local
GALLERY_DEPENDENCIES = sdl2 sdl2_image imgui

define GALLERY_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++17 \
		-I$(STAGING_DIR)/usr/include/SDL2 \
		-o $(@D)/gallery $(@D)/gallery.cpp \
		$(TARGET_LDFLAGS) \
		-lSDL2 -lSDL2_image -limgui
endef

define GALLERY_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/gallery $(TARGET_DIR)/usr/bin/gallery
endef

$(eval $(generic-package))
