# Nombre base del módulo (mem_monitor.c -> mem_monitor.ko)
MODULE_NAME := mem_monitor
# Indica al Kbuild qué objeto externo debe construir como módulo
obj-m += $(MODULE_NAME).o
# Target por defecto cuando se ejecuta `make`
.DEFAULT_GOAL := all

# Árbol de compilación del kernel activo
KDIR ?= /lib/modules/$(shell uname -r)/build
# Directorio actual del proyecto
PWD  := $(CURDIR)

#
# Kbuild no maneja bien rutas con espacios en M=...
# Por eso se crea un staging en /tmp sin espacios.
#
STAGE_DIR := /tmp/$(MODULE_NAME)_kbuild_$(shell id -un)
STAGE_MK  := $(STAGE_DIR)/Makefile

# Prepara carpeta temporal y Makefile mínimo para Kbuild
prepare-stage:
	@mkdir -p "$(STAGE_DIR)"
	@cp "$(PWD)/$(MODULE_NAME).c" "$(STAGE_DIR)/$(MODULE_NAME).c"
	@printf 'obj-m += $(MODULE_NAME).o\n' > "$(STAGE_MK)"

# Compila módulo y copia el .ko de regreso al proyecto
all: prepare-stage
	$(MAKE) -C $(KDIR) M=$(STAGE_DIR) modules
	@cp "$(STAGE_DIR)/$(MODULE_NAME).ko" "$(PWD)/$(MODULE_NAME).ko"

# Limpia artefactos tanto del staging como del proyecto
clean:
	@if [ -d "$(STAGE_DIR)" ]; then \
		$(MAKE) -C $(KDIR) M=$(STAGE_DIR) clean; \
	fi
	@rm -f "$(PWD)/$(MODULE_NAME).ko"
	@rm -rf "$(STAGE_DIR)"

# Carga el módulo; si ya existe, lo recarga
install: all
	@if lsmod | awk '{print $$1}' | grep -qx "$(MODULE_NAME)"; then \
		echo "$(MODULE_NAME) ya estaba cargado, recargando..."; \
		sudo rmmod $(MODULE_NAME); \
	fi
	sudo insmod "$(MODULE_NAME).ko"

# Descarga el módulo si está cargado
uninstall:
	@if lsmod | awk '{print $$1}' | grep -qx "$(MODULE_NAME)"; then \
		sudo rmmod $(MODULE_NAME); \
	else \
		echo "$(MODULE_NAME) no está cargado"; \
	fi

# Flujo rápido para probar salida de /proc
test: install
	@echo ""
	@echo "=== Salida de /proc/mem_monitor ==="
	cat /proc/mem_monitor

# Targets lógicos (no archivos reales)
.PHONY: prepare-stage all clean install uninstall test