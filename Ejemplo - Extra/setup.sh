#!/bin/bash
# Configuracion: grupos y usuarios de prueba

set -e

echo "=== Configuración inicial del daemon ==="
echo

# Crear grupos
echo "[1] Creando grupos del sistema..."
sudo groupadd -f admin_user 2>/dev/null || echo "  (grupo admin_user ya existe)"
sudo groupadd -f common_user 2>/dev/null || echo "  (grupo common_user ya existe)"
echo "  ✓ Grupos listos"
echo

# Crear usuario admin
echo "[2] Creando usuario admin..."
if id "admin1" &>/dev/null; then
    echo "  (admin1 ya existe)"
else
    sudo useradd -m -s /bin/bash admin1
    echo "  ✓ admin1 creado"
fi
sudo usermod -aG admin_user admin1
echo "  ✓ admin1 agregado al grupo admin_user"
echo

# Crear usuario común
echo "[3] Creando usuario común..."
if id "user1" &>/dev/null; then
    echo "  (user1 ya existe)"
else
    sudo useradd -m -s /bin/bash user1
    echo "  ✓ user1 creado"
fi
sudo usermod -aG common_user user1
echo "  ✓ user1 agregado al grupo common_user"
echo

# Establecer contraseñas
echo "[4] Estableciendo contraseñas (para pruebas)..."
echo "admin1:admin1" | sudo chpasswd
echo "  ✓ admin1: contraseña = admin1"

echo "user1:user1" | sudo chpasswd
echo "  ✓ user1: contraseña = user1"
echo

# Crear directorio de monitoreo
echo "[5] Creando directorio de monitoreo..."
mkdir -p ~/monitor
echo "  ✓ ~/monitor listo"
echo

echo "=== Configuración completada ==="
echo
echo "Usuarios de prueba:"
echo "  • admin1 / admin1 (rol ADMIN)"
echo "  • user1 / user1   (rol COMMON)"
echo
echo "Próximos pasos:"
echo "  1. Cargar módulo: sudo insmod file_analyzer/file_analyzer_module.ko"
echo "  2. Arrancar daemon: sudo ./daemon"
echo "  3. Abrir en navegador: http://localhost:8081 (o tu IP:8081)"
echo
