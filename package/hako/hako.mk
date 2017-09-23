HAKO_VERSION = 0.2.0
HAKO_SOURCE = $(HAKO_VERSION).tar.gz
HAKO_SITE = https://github.com/bullno1/hako/archive
HAKO_LICENSE = BSD
HAKO_LICENSE_FILES = LICENSE

define HAKO_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define HAKO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/hako-run $(TARGET_DIR)/sbin/hako-run
endef

define HOST_HAKO_BUILD_CMDS
	$(MAKE) $(HOST_CONFIGURE_OPTS) -C $(@D)
endef

define HOST_HAKO_INSTALL_CMDS
	$(INSTALL) -D -m 0755 $(@D)/hako-run $(HOST_DIR)/sbin/hako-run
	$(INSTALL) -D -m 0755 $(@D)/hako-enter $(HOST_DIR)/sbin/hako-enter
endef

$(eval $(generic-package))
$(eval $(host-generic-package))
