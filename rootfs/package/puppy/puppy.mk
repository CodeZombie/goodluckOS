################################################################################
#
# puppy launcher
#
################################################################################

# Tells Buildroot the source files are in the same folder as this .mk file
PUPPY_LAUNCHER_SITE = $(PUPPY_LAUNCHER_PKGDIR)
PUPPY_LAUNCHER_SITE_METHOD = local

# Ensures SDL2 and its modules are compiled and put into staging BEFORE the program compiles
PUPPY_LAUNCHER_DEPENDENCIES = sdl2 sdl2_image sdl2_ttf sdl2_gfx

# The $(@D) variable points to the build directory where Buildroot copied your source code.
# We include $(TARGET_CXXFLAGS) and $(TARGET_LDFLAGS) to ensure standard Buildroot flags are applied.
define PUPPY_LAUNCHER_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) \
		-I$(STAGING_DIR)/usr/include/SDL2 \
		-o $(@D)/my-program $(@D)/puppy.cpp \
		$(TARGET_LDFLAGS) \
		-lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_gfx
endef

# copies the compiled binary into the final rootfs structure
define PUPPY_LAUNCHER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/puppy $(TARGET_DIR)/usr/bin/puppy
endef

$(eval $(generic-package))
