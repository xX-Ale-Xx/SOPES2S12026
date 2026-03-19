# Semana 8
# Paginación y memoria virtual
---

# Ejemplo de código: `paginacion.c`

En este ejemplo, implementamos un sistema de paginación simple en C. El programa simula la gestión de memoria virtual utilizando una tabla de páginas y un algoritmo de reemplazo de páginas (FIFO). Se crean procesos que acceden a direcciones de memoria virtual, y el programa maneja las traducciones de direcciones y las fallas de página.

* Compilación:

```bash
gcc -o paginacion paginacion.c
```

* Ejecución:

```bash
./paginacion
```

---

# Comandos para ver estadisticas del sistema

1. `htop y top`: Muestran el uso de CPU, memoria y procesos en tiempo real.
    - VIRT (Memoria Virtual): Es el espacio de direcciones lógicas completo del proceso. Incluye todo el código, datos, bibliotecas compartidas y páginas que han sido intercambiadas al disco. 
     - RES (Resident Set Size): Es la parte de la memoria virtual que actualmente reside en marcos de la memoria física (RAM). 
     - SHR (Shared Memory): Memoria que podría estar compartida con otros procesos (como bibliotecas estándar).

Nota: Para el uso del comando htop es necesario instalarlo primero:

```bash
sudo apt install htop
```

2. `vmstat 1`: Muestra estadísticas detalladas sobre el uso de memoria del sistema.

    - swpd: Cantidad de memoria virtual utilizada. 
    - free: Memoria física libre. 
    - si (swap in): Memoria que se está moviendo desde el disco hacia la RAM (paginación por demanda). 
    - so (swap out): Memoria que se está moviendo de la RAM al disco para liberar espacio. 

3. `getconf PAGE_SIZE`: Muestra el tamaño de las páginas de memoria del sistema.

El resultado típico de este comando es 4096 bytes (4 KB), que es el tamaño de página más común en sistemas modernos. Esto significa que cada página de memoria virtual se divide en bloques de 4 KB, y la gestión de memoria se realiza en unidades de estas páginas.