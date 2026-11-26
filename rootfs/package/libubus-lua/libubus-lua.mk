################################################################################
#
# libubus-lua
#
################################################################################

LIBUBUS_LUA_VERSION = faa929a7e720815e972768ce1bb04c4e572a1332
LIBUBUS_LUA_SITE = https://git.openwrt.org/project/ubus.git
LIBUBUS_LUA_SITE_METHOD = git
LIBUBUS_LUA_LICENSE = LGPL-2.1
LIBUBUS_LUA_LICENSE_FILES = ubus.c

LIBUBUS_LUA_SUBDIR = lua
LIBUBUS_LUA_DEPENDENCIES = luainterpreter libubox ubus

LIBUBUS_LUA_CONF_OPTS = \
	-DBUILD_LUA=ON \
	-DLUAPATH=/usr/lib/lua/$(LUAINTERPRETER_ABIVER) \
	-DLUA_CFLAGS="-I$(STAGING_DIR)/usr/include"

define LIBUBUS_LUA_FIX_CMAKE_MINIMUM
	sed -i '1i cmake_minimum_required(VERSION 3.10)\nproject(ubus-lua C)' \
		$(@D)/lua/CMakeLists.txt
endef
LIBUBUS_LUA_POST_PATCH_HOOKS += LIBUBUS_LUA_FIX_CMAKE_MINIMUM

define LIBUBUS_LUA_FIX_LIMITS_H
	sed -i '1i #include <limits.h>' $(@D)/lua/ubus.c
endef
LIBUBUS_LUA_POST_PATCH_HOOKS += LIBUBUS_LUA_FIX_LIMITS_H

$(eval $(cmake-package))
