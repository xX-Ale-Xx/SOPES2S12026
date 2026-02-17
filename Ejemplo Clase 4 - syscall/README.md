# Cómo compilar y probar nueva llamada al sistema (syscall)

## Compilar el kernel con la nueva syscall

1. Guarda todos los cambios en los archivos modificados (`syscall_64.tbl`, `sopes2.c`, `Makefile`).

2. Desde la raíz del código fuente del kernel, compilar el kernel con el comando:
   ```bash
   fakeroot make -j$(nproc)
   ```

3. Instalar el kernel y los módulos:
   ```bash
   sudo make modules_install
   sudo make install
   ```

4. Reinicia y selecciona el nuevo kernel en el menú de arranque:
   ```bash
   sudo reboot
   ```
---

##  Compilar y ejecutar el programa de prueba

1. Compila `test.c`:
   ```bash
   gcc test.c -o test
   ```

2. Ejecuta el programa:

   ```bash
   ./test
   ```

3. Si todo está bien, deberías ver el resultado con las estadísticas de la memoria RAM del sistema. Si hay algún error, el programa imprimirá un mensaje indicando el fallo.

   * Se puede usar `dmesg` para revisar los mensajes del kernel y verificar que la syscall se ejecutó correctamente o para diagnosticar cualquier error que haya ocurrido durante la ejecución.

   * Se puede compara el resultado con el comando `free -m` para verificar que las estadísticas obtenidas por la syscall son correctas.