SINIT_VERSION = 1.0
SINIT_SOURCE = sinit-$(SINIT_VERSION).tar.gz
SINIT_SITE = https://git.suckless.org/sinit/snapshot
SINIT_LICENSE = MIT
SINIT_LICENSE_FILES = LICENSE

define SINIT_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define SINIT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/sinit $(TARGET_DIR)/sbin/init
endef

$(eval $(generic-package))
