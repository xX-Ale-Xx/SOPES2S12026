# Semana 2 - Creación modulo


## Instrucciones para ejecutar el módulo de kernel

0. Instala las herramientas de compilacion
   ```bash
   sudo apt -y install build-essential libncurses-dev bison flex libssl-dev libelf-dev
   ```

1. Abre una terminal y navega al directorio del módulo:
    ```bash
    cd ~/semana2
    ```

3. Compila el módulo usando `make`:
    ```bash
    make
    ```

4. Inserta el módulo en el kernel:
    ```bash
    sudo insmod vmouse.ko
    ```

5. Verifica que el módulo esté cargado:
    ```bash
    lsmod | grep vmouse
    ```

6. Consulta los mensajes del kernel para ver la salida del módulo:
    ```bash
    dmesg | tail
    ```

    Para mover el mouse, escribe en /proc/mouse_control
    ```bash
    echo "10 10" > /proc/mouse_control
    ```
    Mueve el mouse 10 pixeles en X y 10 pixeles en Y

7. Para remover el módulo:
    ```bash
    sudo rmmod vmouse
    ```

8. Limpia los archivos generados:
    ```bash
    make clean
    ```

## ¿Qué hace este módulo?

Este módulo de kernel es un ejemplo básico que, al ser cargado, permite controlar el mouse, escribiendo en **/proc/mouse_control** el dx y dy en formato dos enteros separados por un espacio, ej: "10 10"