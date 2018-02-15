CMP_VERSION = v17
CMP_SITE = $(call github,camgunz,cmp,$(CMP_VERSION))
CMP_LICENSE = MIT
CMP_LICENSE_FILES = LICENSE
CMP_INSTALL_STAGING = YES
CMP_INSTALL_TARGET = NO

define CMP_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -c $(@D)/cmp.c -o $(@D)/cmp.o
	$(TARGET_AR) rcs $(@D)/libcmp.a $(@D)/cmp.o
endef

define CMP_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 0644 $(@D)/libcmp.a $(STAGING_DIR)/lib/
	$(INSTALL) -D -m 0644 $(@D)/cmp.h $(STAGING_DIR)/usr/include
endef

$(eval $(generic-package))
