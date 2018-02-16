HAKOMARI_PROVIDERS_DEPENDENCIES += hakomari

ifeq ($(BR2_PACKAGE_HAKOMARI_PROVIDERS_ETH),y)
HAKOMARI_PROVIDERS_LIST += eth
endif

define HAKOMARI_PROVIDERS_INSTALL_TARGET_CMDS
	$(foreach provider,$(HAKOMARI_PROVIDERS_LIST),$(call HAKOMARI_PROVIDERS_INSTALL_PROVIDER,$(provider)))
endef

define HAKOMARI_PROVIDERS_INSTALL_PROVIDER
	rm -rf $(TARGET_DIR)/usr/lib/hakomari/providers/$1
	mkdir -p $(TARGET_DIR)/usr/lib/hakomari/providers
	cp -R $(@D)/../hakomari/providers/$1 $(TARGET_DIR)/usr/lib/hakomari/providers
endef

$(eval $(generic-package))
