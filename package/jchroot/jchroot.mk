JCHROOT_VERSION = 1.0
JCHROOT_SOURCE = v$(JCHROOT_VERSION).tar.gz
JCHROOT_SITE = https://github.com/vincentbernat/jchroot/archive
JCHROOT_LICENSE = MIT

define JCHROOT_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define JCHROOT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/jchroot $(TARGET_DIR)/sbin/jchroot
endef

define HOST_JCHROOT_BUILD_CMDS
	$(MAKE) $(HOST_CONFIGURE_OPTS) -C $(@D)
endef

define HOST_JCHROOT_INSTALL_CMDS
	$(INSTALL) -D -m 0755 $(@D)/jchroot $(HOST_DIR)/sbin/jchroot
endef

$(eval $(generic-package))
$(eval $(host-generic-package))
