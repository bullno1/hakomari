HAKOMARI_SITE = $(BR2_EXTERNAL_HAKOMARI_PATH)/src
HAKOMARI_SITE_METHOD = local
HAKOMARI_DEPENDENCIES += cmp libancillary

define HAKOMARI_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define HAKOMARI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/hakomari-dispatcherd $(TARGET_DIR)/sbin/
	$(INSTALL) -D -m 0755 $(@D)/hakomari-displayd $(TARGET_DIR)/sbin/
	$(INSTALL) -D -m 0755 $(@D)/hakomari-endpointd $(TARGET_DIR)/bin/
	$(INSTALL) -D -m 0755 $(@D)/hakomari-vaultd $(TARGET_DIR)/bin/
	$(INSTALL) -D -m 0755 $(@D)/hakomari-show $(TARGET_DIR)/bin/
	$(INSTALL) -D -m 0755 $(@D)/hakomari-ask-passphrase $(TARGET_DIR)/bin/
	rm -f $(TARGET_DIR)/bin/hakomari-confirm
	ln -s hakomari-show $(TARGET_DIR)/bin/hakomari-confirm
endef

define HAKOMARI_USERS
	hakomari-displayd -1 hakomari-daemon -1 * - - - Display daemon
	hakomari-dispatcherd -1 hakomari-daemon -1 * - - - Dispatcher daemon
	hakomari-endpointd -1 hakomari-daemon -1 * - - - Endpoint daemon
	hakomari-vaultd -1 hakomari-daemon -1 * - - - Vault daemon
endef

$(eval $(generic-package))
