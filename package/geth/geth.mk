GETH_VERSION = v1.8.0
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
		build -v -o $(@D)/bin/ethkey -ldflags "-extldflags '-static'" ./cmd/ethkey
endef

define GETH_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/bin/ethkey $(TARGET_DIR)/bin/ethkey
endef

$(eval $(generic-package))
