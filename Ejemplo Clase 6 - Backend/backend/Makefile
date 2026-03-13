# Compilador C
CC := gcc
# Flags de compilación: optimización + warnings + estándar C11
CFLAGS := -O2 -Wall -Wextra -std=c11
# Nombre del binario final
TARGET := mem_ws_backend
# Utilidad para obtener flags de librerías instaladas
PKG_CONFIG ?= pkg-config
# Flags de compilación para libwebsockets (includes, defines, etc.)
LWS_CFLAGS := $(shell $(PKG_CONFIG) --cflags libwebsockets 2>/dev/null)
# Flags de link para libwebsockets (libs y rutas)
LWS_LIBS := $(shell $(PKG_CONFIG) --libs libwebsockets 2>/dev/null)

# Target por defecto cuando ejecutas: make
all: $(TARGET)

# Verifica que exista libwebsockets en el sistema
check-deps:
	@$(PKG_CONFIG) --exists libwebsockets || (echo "Falta dependencia: libwebsockets" && echo "Instala con: sudo apt install -y libwebsockets-dev pkgconf" && exit 1)

# Regla de compilación principal
# $@ = target actual (mem_ws_backend)
# $< = primera dependencia (mem_ws_backend.c)
$(TARGET): mem_ws_backend.c check-deps
	$(CC) $(CFLAGS) $(LWS_CFLAGS) -o $@ $< $(LWS_LIBS)

# Elimina binarios generados
clean:
	rm -f $(TARGET)

# Ejecuta el servidor (compila antes si hace falta)
run: $(TARGET)
	./$(TARGET)

# Estos targets no representan archivos reales
.PHONY: all check-deps clean run
