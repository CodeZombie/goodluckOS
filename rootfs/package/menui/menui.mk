################################################################################
#
# menui
#
################################################################################

MENUI_VERSION = 1.0
MENUI_SITE = $(MENUI_PKGDIR)/src
MENUI_SITE_METHOD = local
MENUI_LICENSE = MIT

MENUI_INSTALL_STAGING = NO

MENUI_DEPENDENCIES = host-pkgconf luajit sdl2 sdl2_ttf

$(eval $(cmake-package))
