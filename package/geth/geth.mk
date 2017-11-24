GETH_VERSION = 4bb3c89d44e372e6a9ab85a8be0c9345265c763a
GETH_SITE = $(call github,ethereum,go-ethereum,$(GETH_VERSION))
GETH_LICENSE = LGPL
GETH_LICENSE_FILES = COPYING
GETH_DEPENDENCIES = host-go

GETH_MAKE_ENV = \
	$(HOST_GO_TARGET_ENV) \
	CGO_ENABLED=1 \
	GOBIN="$(@D)/bin" \
	GOPATH="$(@D)/gopath"

define GETH_CONFIGURE_CMDS
	mkdir -p $(@D)/gopath/src/github.com/ethereum
	ln -s $(@D) $(@D)/gopath/src/github.com/ethereum/go-ethereum
endef

define GETH_BUILD_CMDS
	cd $(@D)/gopath/src/github.com/ethereum/go-ethereum && $(GETH_MAKE_ENV) $(HOST_DIR)/bin/go \
		build -v -o $(@D)/bin/geth -ldflags "-extldflags '-static'" ./cmd/geth
endef

define GETH_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/bin/geth $(TARGET_DIR)/bin/geth
endef

$(eval $(generic-package))
