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
   Recibe un void* para ser compatible con pthread, luego se castea a char* */
void* myFunction(void* name){
    /* Imprime un mensaje al comenzar, identificando qué hilo es */
    printf("Imprimiendo desde %s\n", (char*) name);

    /* Simula una tarea que tarda tiempo (5 segundos) */
    sleep(5);

    /* Imprime un mensaje al finalizar la tarea */
    printf("Trabajo realizado - %s\n", (char*) name);
}

int main(){
    /* Declaración de los identificadores de los dos hilos */
    pthread_t thread1, thread2;
    int err; /* Variable reservada para capturar errores (no usada aquí) */
	
    /* Creación del primer hilo: ejecutará myFunction con el argumento "Hilo 1".
       - &thread1: identificador donde se almacena el hilo creado
       - NULL: atributos por defecto
       - myFunction: función que ejecutará el hilo
       - "Hilo 1": argumento que se le pasa a la función */
    pthread_create(&thread1, NULL, myFunction, "Hilo 1");

    /* Creación del segundo hilo, igual que el anterior pero con "Hilo 2" */
    pthread_create(&thread2, NULL, myFunction, "Hilo 2");

    printf("Esperando a los hilos...\n");

    /* pthread_join bloquea el hilo principal hasta que thread1 termine.
       El segundo NULL indica que no nos interesa el valor de retorno del hilo */
    pthread_join(thread1, NULL);

    /* Espera a que thread2 también termine antes de continuar */
    pthread_join(thread2, NULL);

    printf("Trabajo terminado\n");
    return 0;
}
