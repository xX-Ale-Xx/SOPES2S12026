#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#define NUM_HILOS 5

// Recursos compartidos
int registro_impresiones = 0; // Variable que genera la Condición de Carrera
pthread_mutex_t mutex_registro; 
sem_t semaforo_accesos;

void* rutina_hilo(void* arg) {
    int id_hilo = *(int*)arg;

    printf("Hilo %d: Intentando acceder al servidor...\n", id_hilo);

    // Controlar que solo 2 hilos pasen de este punto
    sem_wait(&semaforo_accesos); 
    printf("Hilo %d: Entrando al servidor. Preparando documento...\n", id_hilo);
    sleep(2);

    // Bloquear el mutex para modificar el registro de impresiones
    pthread_mutex_lock(&mutex_registro); 
    
    // Sección crítica
    printf("Hilo %d: Modificando el registro (Sección Crítica).\n", id_hilo);
    int temporal = registro_impresiones;
    temporal++; // Modificación del registro sin protección (condición de carrera)
    sleep(2);   // Simulacion demora para forzar el problema si no hubiera mutex
    registro_impresiones = temporal;
    printf("Hilo %d: Nuevo total de impresiones: %d.\n", id_hilo, registro_impresiones);

    // Desbloquear el mutex para permitir que otros hilos modifiquen el registro
    pthread_mutex_unlock(&mutex_registro);

    // Liberar semáforo para permitir que otro hilo acceda al servidor
    printf("Hilo %d: Saliendo del servidor.\n", id_hilo);
    sem_post(&semaforo_accesos); 

    free(arg);
    return NULL;
}

int main() {
    pthread_t hilos[NUM_HILOS];

    // Mutex
    pthread_mutex_init(&mutex_registro, NULL);
    
    // Semáforo que permite 2 hilos
    sem_init(&semaforo_accesos, 0, 2);

    // Creación hilos
    for (int i = 0; i < NUM_HILOS; i++) {
        int* id = malloc(sizeof(int));
        *id = i + 1;
        pthread_create(&hilos[i], NULL, rutina_hilo, id);
    }

    // Esperar que los hilos terminen
    for (int i = 0; i < NUM_HILOS; i++) {
        pthread_join(hilos[i], NULL);
    }

    // Destruir mutex y semáforo
    pthread_mutex_destroy(&mutex_registro);
    sem_destroy(&semaforo_accesos);

    printf("Programa finalizado. Total real de impresiones: %d\n", registro_impresiones);
    return 0;
}