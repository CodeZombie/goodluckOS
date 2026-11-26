################################################################################
#
# libubox-lua
#
################################################################################

LIBUBOX_LUA_VERSION = ecddb31dc34d89be5c9dc4595a4c58070eb090e4
LIBUBOX_LUA_SITE = https://git.openwrt.org/project/libubox.git
LIBUBOX_LUA_SITE_METHOD = git
LIBUBOX_LUA_LICENSE = ISC
LIBUBOX_LUA_LICENSE_FILES = lua/uloop.c

LIBUBOX_LUA_SUBDIR = lua
LIBUBOX_LUA_DEPENDENCIES = luainterpreter libubox

LIBUBOX_LUA_CONF_OPTS = \
	-DBUILD_LUA=ON \
	-DLUAPATH=/usr/lib/lua/$(LUAINTERPRETER_ABIVER) \
	-DLUA_CFLAGS="-I$(STAGING_DIR)/usr/include"

define LIBUBOX_LUA_FIX_CMAKE_MINIMUM
	sed -i '1i cmake_minimum_required(VERSION 3.10)\nproject(uloop-lua C)' \
		$(@D)/lua/CMakeLists.txt
endef
LIBUBOX_LUA_POST_PATCH_HOOKS += LIBUBOX_LUA_FIX_CMAKE_MINIMUM

define LIBUBOX_LUA_FIX_LUA_COMPAT
	sed -i -e 's/luaI_openlib/luaL_openlib/g' \
	       -e 's/luaL_reg/luaL_Reg/g' \
	       $(@D)/lua/uloop.c
endef
LIBUBOX_LUA_POST_PATCH_HOOKS += LIBUBOX_LUA_FIX_LUA_COMPAT

$(eval $(cmake-package))
