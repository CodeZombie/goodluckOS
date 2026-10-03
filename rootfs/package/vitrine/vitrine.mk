################################################################################
#
# vitrine
#
################################################################################

VITRINE_SITE = $(VITRINE_PKGDIR)
VITRINE_SITE_METHOD = local

VITRINE_DEPENDENCIES = sdl2 sdl2_image sdl2_ttf

define VITRINE_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++17 \
		-o $(@D)/vitrine $(@D)/vitrine.cpp \
		$(TARGET_LDFLAGS) \
		-lpthread -lSDL2 -lSDL2_image -lSDL2_ttf
endef

define VITRINE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/vitrine $(TARGET_DIR)/usr/bin/vitrine
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/usr/share/vitrine/fonts
	$(INSTALL) -m 0644 $(@D)/fonts/* $(TARGET_DIR)/usr/share/vitrine/fonts/
endef

$(eval $(generic-package))
