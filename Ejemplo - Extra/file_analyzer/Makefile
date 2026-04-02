obj-m += file_analyzer_module.o

# Necesita la librería de crypto del kernel para SHA-256
KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)
BUILD_DIR := /tmp/file_analyzer_kbuild_src

all:
	rm -rf "$(BUILD_DIR)"
	mkdir -p "$(BUILD_DIR)"
	cp -f "$(PWD)/file_analyzer_module.c" "$(PWD)/Makefile" "$(BUILD_DIR)/"
	$(MAKE) -C $(KDIR) M=$(BUILD_DIR) modules
	cp -f "$(BUILD_DIR)/file_analyzer_module.ko" "$(PWD)/"
	[ -f "$(BUILD_DIR)/Module.symvers" ] && cp -f "$(BUILD_DIR)/Module.symvers" "$(PWD)/" || true
	[ -f "$(BUILD_DIR)/modules.order" ] && cp -f "$(BUILD_DIR)/modules.order" "$(PWD)/" || true

clean:
	[ -d "$(BUILD_DIR)" ] && $(MAKE) -C $(KDIR) M=$(BUILD_DIR) clean || true
	rm -rf "$(BUILD_DIR)"
	rm -f "$(PWD)/file_analyzer_module.ko" "$(PWD)/Module.symvers" "$(PWD)/modules.order"
