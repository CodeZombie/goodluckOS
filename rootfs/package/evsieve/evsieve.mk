################################################################################
#
# evsieve
#
################################################################################

EVSIEVE_VERSION = 2b1bcd046829db69617f2a47d8d554f20cd0ec08
EVSIEVE_SITE = $(call github,KarsMulder,evsieve,$(EVSIEVE_VERSION))
EVSIEVE_LICENSE = GPL-2.0+
EVSIEVE_LICENSE_FILES = COPYING

EVSIEVE_DEPENDENCIES = libevdev

# The pre-generated bindings contain bindgen layout assertions that hardcode
# 64-bit struct sizes/alignments/offsets. The structs themselves use c_long
# and are fine on 32-bit, so drop only the assertions.
define EVSIEVE_FIX_BINDINGS_FOR_32BIT
	perl -0pi -e 's/(?:^#\[[^\n]*\]\n)*^const _: \(\) = \{.*?^\};\n//msg' \
		$(@D)/src/bindings/libevdev.rs
endef
EVSIEVE_POST_PATCH_HOOKS += EVSIEVE_FIX_BINDINGS_FOR_32BIT

$(eval $(cargo-package))
