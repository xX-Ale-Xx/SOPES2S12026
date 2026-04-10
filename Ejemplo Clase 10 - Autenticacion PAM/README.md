# Semana 10 : Protección y Seguridad
# Utilización de PAM (Pluggable Authentication Modules) para la autenticación y autorización de usuarios en Linux.

## Crear usuarios y roles

```bash
# Crear un nuevo usuario
sudo useradd -m usuario1
sudo useradd -m usuario2

# Establecer una contraseña para el nuevo usuario
sudo passwd usuario1
sudo passwd usuario2

# Crear un nuevo grupo (rol)
sudo groupadd rol_desarrollador
sudo groupadd rol_administrador

# Asignar el usuario al grupo (rol)
sudo usermod -aG rol_desarrollador usuario1
sudo usermod -aG rol_administrador usuario2 

# Verificar la asignación del usuario al grupo
groups usuario1
groups usuario2
```

* Comando para ver el usuario actual: `who am i`
* Comando para ver el UID del usuario actual: `id -u`
* Comando para ver el grupo del usuario actual: `id -Gn`
* Comando para ver el UID y GID de un usuario específico: `id <nombre_usuario>`
* Comando para ver el GID de un grupo específico: `getent group <nombre_grupo>`

## Cambio de usuario

```bash
# Cambiar a otro usuario
su - usuario1
su - usuario2

# Verificar el cambio de usuario
who am i
id -u
id -Gn
```

## Ejecución pruebas PAM

* Compilación:


```bash
# Instalar las bibliotecas de desarrollo PAM
sudo apt install libpam0g-dev -y

# Compilar el programa de prueba PAM
gcc -Wall -Wextra -o test test.c pam.c -lpam -lpam_misc
```

* Ejecución:

```bash
# Ejecutar el programa de prueba PAM
./test usuario1 usuario1 rol_desarrollador 
./test usuario2 usuario2 rol_administrador

# Error de autenticación
./test usuario1 password rol_desarrollador

# Error de grupo
./test usuario1 usuario1 rol_administrador
```
