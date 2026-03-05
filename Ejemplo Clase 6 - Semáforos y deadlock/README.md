# Sincronización con Mutex y Semáforos

## Compilación de codigo

* Compilación de los programas de sincronización y deadlock:

```bash
gcc -o sincronizacion sincronizacion.c -lpthread
gcc -o deadlock deadlock.c -lpthread
```

* Ejecución de los programas:

```bash
./sincronizacion
```

```bash
./deadlock
```

## 1. Mutex y semáforo: Controlando el acceso a un recurso compartido

Imagina que tenemos 5 procesos (hilos) compitiendo por entrar a un servidor. El servidor solo permite que 2 hilos estén activos al mismo tiempo (controlado por un semáforo). Una vez adentro, los hilos deben actualizar un registro global de impresiones, pero solo 1 hilo a la vez puede modificar ese registro para no arruinar los datos (controlado por un mutex).

* El Problema: Tenemos 5 hilos que comparten el mismo espacio de direcciones, específicamente la variable `registro_impresiones`. Si varios hilos intentan modificar esta variable al mismo tiempo sin control, ocurre una condición de carrera, y terminaríamos con resultados incorrectos (por ejemplo, contar 3 impresiones en lugar de 5).

* Sección crítica: Las líneas donde leemos, sumamos y guardamos la variable `registro_impresiones` forman nuestra sección crítica. Para mantener la coherencia de los datos, este bloque de código debe ejecutarse de forma atómica, es decir, en una unidad ininterrumpible.

* Uso del Semáforo (`sem_t`): En el código utilizamos `sem_wait` y `sem_post` (las funciones de C equivalentes a wait y signal). Un semáforo es una variable entera a la que solo se accede mediante estas dos operaciones atómicas. Lo inicializamos en 2, lo que significa que toma valores mayores que uno para controlar el acceso a un recurso que consta de un número finito de instancias (el servidor). Si 2 hilos ya entraron, el valor es 0, y cualquier otro proceso que haga `wait` se bloqueará hasta que alguien haga `signal` (o `sem_post`).

* Uso del Mutex (`pthread_mutex_t`): Mientras el semáforo cuenta "cuántos pueden pasar", el mutex lo usamos estrictamente para administrar la exclusión mutua de nuestra sección crítica. Un mutex solo tiene dos estados: *desbloqueado* o *bloqueado*. Cuando un hilo llama a `pthread_mutex_lock`, cierra la puerta. Si otro hilo llega a esa línea y el mutex ya está cerrado, se bloquea y espera hasta que el primer hilo llame a `pthread_mutex_unlock`.

## 2. Deadlock: Acceso bloqueado entre hilos

Un deadlock o bloqueo mutuo es el bloqueo permanente de un conjunto de procesos o hilos de ejecución en un sistema concurrente que compiten por recursos del sistema. En nuestro código, el **hilo 1** entró en un estado de espera porque el **mutex 2** está retenido por el **hilo 2**, que a su vez está esperando el **mutex 1** retenido por el **hilo 1**. Como ambos son incapaces de cambiar su estado indefinidamente, el sistema está en deadlock.

Explicación del fallo:

1. Exclusión mutua: Solo un hilo a la vez puede usar el **recurso 1** o el **recurso 2** (logrado gracias al `pthread_mutex_lock`).
2. Retener y esperar: El **hilo 1** retiene el **recurso 1** y está esperando el **recurso 2**.
3. Sin preferencia: El sistema operativo no le puede "arrebatar" el **recurso 2** al **hilo 2** para dárselo al **hilo 1**.
4. Espera circular: El **hilo 1** espera al **hilo 2**, y el **hilo 2** espera al **hilo 1**, formando un ciclo infinito.

Como se podria solucionar:

1. Escribir un protocolo estricto en el código (por ejemplo, obligar a que todos los hilos siempre bloqueen el recurso 1 primero y luego el 2) para asegurar que el sistema nunca entrará en deadlock.
2. Permitir que ocurra, detectarlo con un algoritmo y matar a uno de los hilos para recuperarse.
3. Ignorar el problema por completo si es algo que ocurre una vez cada 10 años (como hacen algunos sistemas operativos de usuario final).