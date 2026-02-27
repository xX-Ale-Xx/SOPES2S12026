#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>  
#include <sys/wait.h>

/* Estructura para pasar múltiples argumentos a un hilo.
   Aunque no se usa en este ejemplo, sirve como plantilla. */
struct args {
    int numero;
    char* cadena;
}; 

/* Función que ejecutará cada hilo.
   Recibe un void* que se castea a int* para obtener el ID del hilo */
void* myFunction(void* id){
    /* Se castea el argumento genérico void* a int* para poder leer el número */
    int *thread_id = (int*)id;

    printf("Imprimiendo desde Hilo %d\n", *thread_id);

    /* Simula una tarea que tarda tiempo (5 segundos) */
    sleep(5);

    printf("Trabajo realizado - Hilo %d\n", *thread_id);

    /* Buena práctica: liberar la memoria reservada con malloc dentro del hilo
       (en este código no se hace, pero sería recomendable hacerlo aquí) */
}

int main(){
	
    /* Array que almacena los identificadores de los 5 hilos */
    pthread_t threads[5];
  
    /* Crear los 5 hilos en un loop */
    for(int i = 0; i < 5; i++){ 
        /* Se reserva memoria dinámica para el ID de cada hilo.
           Es necesario usar malloc porque la variable debe sobrevivir
           fuera del scope del loop; si usáramos &i, todos los hilos
           compartirían la misma dirección de memoria y leerían el mismo valor */
        int *a = malloc(sizeof(int));
        *a = i; /* Se guarda el índice actual como ID del hilo */

        /* Se crea el hilo i, pasándole su ID como argumento */
        pthread_create(&threads[i], NULL, myFunction, (void *) a);
    }   
    
    /* Esperar a que todos los hilos terminen antes de cerrar el programa */
    for(int i = 0; i < 5; i++){
        /* pthread_join bloquea el main hasta que el hilo i finalice */
        pthread_join(threads[i], NULL);
    }   

    return 0;
}