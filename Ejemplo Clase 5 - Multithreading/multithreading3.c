#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>  
#include <sys/wait.h>

/* Estructura que agrupa los argumentos que se le pasarán al hilo.
   Permite enviar múltiples datos en un solo void* */
struct args {
    int numero;
    char* cadena;
}; 

/* Función que ejecutará el hilo.
   Recibe un void* que se castea a (struct args*) para acceder a los campos */
void* myFunction(void* input){
    /* Se accede al campo 'cadena' casteando input a puntero de struct args */
    printf("Cadena: %s\n", ((struct args*)input)->cadena);

    /* Se accede al campo 'numero' de la misma forma */
    printf("Numero: %d\n", ((struct args*)input)->numero);

    /* Simula una tarea que tarda tiempo */
    sleep(5);

    printf("Trabajo realizado\n");
}

int main(){
    pthread_t thread; /* Identificador del hilo */
    int err;          /* Almacena el código de error de pthread_create */

    /* Se define la cadena que se pasará como argumento */
    char cadena[] = "Hola Mundo";

    /* Se crea e inicializa el struct con los argumentos para el hilo */
    struct args myArgs;
    myArgs.cadena = cadena;
    myArgs.numero = 24;

    /* Se crea el hilo pasando &myArgs casteado a void*.
       IMPORTANTE: myArgs debe seguir existiendo mientras el hilo la use,
       por eso se declara en main y no dentro de un bloque más interno */
    err = pthread_create(&thread, NULL, myFunction, (void *) &myArgs);

    /* Se verifica si hubo error al crear el hilo.
       pthread_create devuelve 0 en éxito, o un código de error distinto de 0 */
    if(err){
        printf("Error al crear el hilo: %d\n", err);
        return 1;
    }

    printf("Esperando al hilo...\n");

    /* Bloquea el main hasta que el hilo termine su ejecución */
    pthread_join(thread, NULL);

    printf("Hilo terminado\n");
    return 0;
}