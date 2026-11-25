################################################################################
#
# ini-lua
#
################################################################################

# Pin to a full commit SHA of the branch you want (default branch: docking_inter)
INI_LUA_VERSION = origin/master
INI_LUA_SITE = https://github.com/lzubiaur/ini.lua
INI_LUA_SITE_METHOD = git
INI_LUA_LICENSE = MIT
INI_LUA_LICENSE_FILES = LICENSE.md

INI_LUA_DEPENDENCIES = luajit

define INI_LUA_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/usr/share/lua/5.1
	cp -dpfr $(@D)/ini.lua $(TARGET_DIR)/usr/share/lua/5.1/
endef

$(eval $(generic-package))
