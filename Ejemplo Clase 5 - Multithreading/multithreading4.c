#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>  
#include <sys/wait.h>

/* Función que ejecutará cada hilo.
   Su propósito es demostrar el problema de condición de carrera
   con variables compartidas entre hilos. */
void* myFunction(void *arg)
{
    /* Se obtiene el número de hilo casteando el void* a int* */
    int n = *(int*)arg;

    /* Variable estática: existe UNA SOLA copia compartida entre TODOS los hilos.
       Al ser estática, no se reinicia en cada llamada a la función.
       Esto la hace vulnerable a condiciones de carrera. */
    static int x = 10;

    /* Variable local automática: cada hilo tiene SU PROPIA COPIA en su stack.
       Se inicializa con el valor actual de x en este momento. */
    int y = x;

    /* Aquí x e y deberían ser iguales, ya que y acaba de copiarse de x.
       Sin embargo, otro hilo podría modificar x justo después de esta línea,
       haciendo que x e y diverjan más adelante. */
    printf("Thread %d: x = %d, y = %d\n", n, x, y);

    /* Aquí es donde ocurre la CONDICIÓN DE CARRERA:
       Entre la asignación y = x (arriba) y esta comparación,
       otro hilo pudo haber incrementado x.
       Por eso x e y a veces son distintos, aunque no debería ocurrir
       si el acceso fuera seguro (thread-safe). */
    if (x != y) {
        printf("Thread %d: Error! x != y! %d != %d\n", n, x, y);
    }

    /* Incremento de la variable compartida.
       Esta operación NO es atómica: internamente es leer x, sumar 1 y escribir.
       Si dos hilos ejecutan esto al mismo tiempo, uno puede sobreescribir
       el incremento del otro (pérdida de actualización). */
    x++;

    return 0;
}

/* Número de hilos que se crearán */
#define THREAD_COUNT 5

int main(void){	
    pthread_t thread[THREAD_COUNT];

    /* Se crean los 5 hilos en un loop */
    for (int i = 0; i < THREAD_COUNT; i++) {
        /* malloc para que cada hilo tenga su propio número independiente.
           Si pasáramos &i, todos los hilos leerían el mismo i que el loop modifica. */
        int *n = malloc(sizeof *n);
        *n = i;
        pthread_create(&thread[i], NULL, myFunction, (void *) n);
    }

    /* Se espera a que todos los hilos terminen antes de cerrar el programa */
    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_join(thread[i], NULL);
    }
}