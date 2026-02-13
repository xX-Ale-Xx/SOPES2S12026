# Cómo compilar e instalar el kernel de Linux

## Instalar dependencias

Primero necesitamos instalar algunos paquetes necesarios para la compilación:

- **build-essential**
    
    Meta-paquete que instala lo básico para compilar en C/C++: `gcc`, `g++`, `make`, headers estándar y herramientas necesarias para construir software desde código fuente.
    
- **libncurses-dev**
    
    Librería para interfaces en terminal (modo texto). Se usa mucho en menús interactivos tipo `make menuconfig` al configurar el kernel.
    
- **bison**
    
    Generador de analizadores sintácticos (parser). Convierte definiciones gramaticales en código C. Se usa cuando el proyecto necesita procesar lenguajes o estructuras complejas.
    
- **flex**
    
    Generador de analizadores léxicos. Trabaja junto con bison para reconocer tokens (palabras clave, símbolos, etc.) en texto fuente.
    
- **libssl-dev**
    
    Headers y librerías de desarrollo de OpenSSL. Permite compilar software que use criptografía, firmas digitales, hashes y conexiones seguras.
    
- **libelf-dev**
    
    Librería para manejar archivos ELF (Executable and Linkable Format), el formato estándar de binarios en Linux. El kernel y herramientas de bajo nivel la utilizan para leer/analizar ejecutables.
    
- **fakeroot**
    
    Simula permisos de superusuario durante la creación de paquetes o builds. Permite generar archivos con “propietario root” sin ser root realmente.
    
- **dwarves**
    
    Incluye herramientas como `pahole`. Sirve para analizar información de depuración DWARF y optimizar estructuras internas; el kernel lo usa para generar metadata (por ejemplo, BTF).
    

```bash
 sudo apt install build-essential libncurses-dev bison flex libssl-dev libelf-dev fakeroot dwarves
```

## Descargar y descomprimir el kernel

A continuación, debemos descargar el codigo fuente del kernel desde el sitio web oficial [kernel.org](http://kernel.org/).

![image.png](./imgs/kernelorg.png)

Usaremos la version `longterm` del kernel (En este curso usaremos el siguiente: https://www.kernel.org/pub/linux/kernel/v6.x/linux-6.12.69.tar.xz).  Copie el enlace del hipervínculo `tarball`. Luego use este enlace para descargar y descomprimir la fuente del kernel.

```bash
wget <https://www.kernel.org/pub/linux/kernel/v6.x/linux-6.12.69.tar.xz>
tar -xf linux-6.12.69.tar.xz
```

## Configurar el kernel

Primero ingrasamos al directorio del codigo fuente:

```bash
cd linux-6.12.69
```

La configuración del kernel se debe especificar en un archivo .config. Para no escribir este desde 0 vamos a copiar el archivo de configuración de su Linux actualmente instalado.

El archivo `.config` define **qué características tendrá el kernel cuando se compile**: drivers incluidos, soporte de hardware, sistemas de archivos, seguridad, módulos, etc. No es un archivo cualquiera; es el “perfil” que determina cómo se construirá el kernel.

Copiar la configuración del Linux que ya tienes instalado se hace por razones prácticas:

- **Porque ya funciona en tu hardware**
    
    Tu sistema actual arranca, reconoce tu disco, red, USB, gráficos, etc. Eso significa que ese kernel fue compilado con las opciones correctas. Si partes desde esa configuración, reduces muchísimo el riesgo de que el nuevo kernel no arranque o no detecte algo importante.
    
- **Porque crearla desde cero es complejo**
    
    El kernel tiene miles de opciones. Elegirlas manualmente sin experiencia puede provocar errores como:
    
    - No incluir el driver del disco → el sistema no arranca.
    - Quitar soporte del sistema de archivos → no puede montar el root.
    - Desactivar algo crítico sin darte cuenta.
- **Porque ahorra tiempo**
    
    En vez de decidir todo, tomas una base estable y solo cambias lo que te interesa (por ejemplo, activar un módulo, desactivar algo que no usas, o experimentar con una feature).
    
- **Porque mantiene compatibilidad**
    
    Tu configuración actual ya está alineada con tu CPU, chipset, BIOS/UEFI y periféricos. Copiarla garantiza que el nuevo kernel siga soportando lo mismo.
    

```bash
cp -v /boot/config-$(uname -r) .config
```

Sin embargo, este esta lleno de modulos y drivers que no necesitamos que pueden aumentar el tiempo de compilación. Por lo que utilizamos el comando localmodconfig que analiza los módulos del kernel cargados de su sistema y modifica el archivo .config de modo que solo estos módulos se incluyan en la compilación. Tomar en cuenta que esto causará que nuestro kernel compilado solo funcione en nuestra maquina, por lo que si quieren que sea portatil omitan este paso.

```bash
make localmodconfig
```

Luego tenemos que modificar el .config, ya que al copiar nuestro .config se incluyeron nuestras llaves privadas, por lo que tendremos que reliminarlas del .config.

```bash
$ scripts/config --disable SYSTEM_TRUSTED_KEYS
$ scripts/config --disable SYSTEM_REVOCATION_KEYS
$ scripts/config --set-str CONFIG_SYSTEM_TRUSTED_KEYS ""
$ scripts/config --set-str CONFIG_SYSTEM_REVOCATION_KEYS ""
```

## Compilar el kernel

Ahora es el momento de compilar el kernel. Para esto simplemente ejecute el comando:

```bash
$ fakeroot make
```

Utilizar `fakeroot` es necesario por que nos permite ejecutar el comando `make` en  un  entorno  donde  parece  que  se  tiene  permisos  de superusuario  para  la  manipulación  de  ficheros.  Es necesario  para  permitir a este comando crear archivos (tar, ar, .deb etc.) con ficheros con permisos/propietarios de superusuario.

También se puede acelerar la compilación utilizando varios núcleos de CPU. Por ejemplo:

```bash
$ fakeroot make -j4
```

utilizará cuatro núcleos de su computadora.

Si el proceso de compilación falla puede ejecutar el comando:

```bash
$ echo $?
```

para obtener el codigo de error.

## Instalar el kernel

La instalación se divide en dos partes: instalar los módulos del kernel e instalar el kernel mismo.

Primero se instalan los módulos del kernel ejecutando:

```bash
$ sudo make modules_install
```

Luego instalamos el kernel:

```bash
$ sudo make install
```

### Nota: Acceder a GRUB y seleccionar el nuevo kernel

Cuando se instala y compila un nuevo kernel, este se agrega al gestor de arranque (GRUB), pero no siempre se selecciona automáticamente como opción principal. Por eso, es importante saber cómo entrar al menú y elegir manualmente qué kernel iniciar.

Al reiniciar la máquina virtual:

- Justo después de encenderla, mantén presionada la tecla **SHIFT** (en algunas VMs puede ser **ESC**) para forzar que aparezca el menú de GRUB.
- Este menú es el gestor de arranque que permite escoger con qué sistema o kernel iniciar.

Dentro de GRUB verás varias opciones, por ejemplo:

- El sistema Linux normal (opción por defecto).
- Opciones avanzadas (Advanced options).
- Diferentes versiones de kernel instaladas.

Entra a **“Advanced options for Linux”** y ahí aparecerá una lista con los kernels disponibles, por ejemplo:

- Kernel antiguo (el que ya usabas antes).
- Kernel nuevo (el que acabas de compilar).

Selecciona el kernel nuevo y presiona Enter para iniciar con esa versión.

Esto es importante porque:

- Permite probar el kernel recién compilado sin eliminar el anterior.
- Si algo falla (pantalla negra, no detecta red, no arranca), puedes volver a GRUB y elegir el kernel viejo para recuperar el sistema.

### Reinicio posterior

Después de iniciar correctamente con el nuevo kernel y verificar que todo funciona bien (red, disco, sistema estable), se recomienda reiniciar nuevamente el sistema. Esto ayuda a completar cualquier configuración pendiente y confirmar que el kernel se carga correctamente desde el arranque normal.

Para comprobar que realmente estás usando el nuevo kernel, puedes ejecutar:

```bash
uname -r
```

Ese comando mostrará la versión del kernel activo. Si coincide con la versión que compilaste, significa que la instalación se realizó correctamente.