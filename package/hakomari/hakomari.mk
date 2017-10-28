HAKOMARI_SITE = $(BR2_EXTERNAL_HAKOMARI_PATH)/src
HAKOMARI_SITE_METHOD = local
HAKOMARI_DEPENDENCIES += cmp

define HAKOMARI_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define HAKOMARI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/hakomari-displayd $(TARGET_DIR)/sbin/hakomari-displayd
endef

$(eval $(generic-package))
