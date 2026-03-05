#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

// recursos compartidos protegidos por mutex
pthread_mutex_t mutex_recurso1;
pthread_mutex_t mutex_recurso2;

void* rutina_hilo1(void* arg) {

    // Bloquear recurso 1
    printf("Hilo 1: Intentando bloquear el Recurso 1...\n");
    pthread_mutex_lock(&mutex_recurso1);
    printf("Hilo 1: Bloqueado el Recurso 1. Procesando...\n");
    
    sleep(2); 


    // Bloquear recurso 2 (Error aqui: el hilo 2 ya lo tiene bloqueado)
    printf("Hilo 1: Intentando bloquear el Recurso 2...\n");
    pthread_mutex_lock(&mutex_recurso2); 
    printf("Hilo 1: Bloqueado el Recurso 2. ¡Éxito!\n");
    
    // Liberar recursos
    pthread_mutex_unlock(&mutex_recurso2);
    pthread_mutex_unlock(&mutex_recurso1);
    return NULL;
}

void* rutina_hilo2(void* arg) {

    // Bloquear recurso 2
    printf("Hilo 2: Intentando bloquear el Recurso 2...\n");
    pthread_mutex_lock(&mutex_recurso2);
    printf("Hilo 2: Bloqueado el Recurso 2. Procesando...\n");
    
    sleep(2); 
    
    // Bloquear recurso 1 (Error aqui: el hilo 1 ya lo tiene bloqueado)
    printf("Hilo 2: Intentando bloquear el Recurso 1...\n");
    pthread_mutex_lock(&mutex_recurso1); 
    printf("Hilo 2: Bloqueado el Recurso 1. ¡Éxito!\n");
    
    // Liberar recursos
    pthread_mutex_unlock(&mutex_recurso1);
    pthread_mutex_unlock(&mutex_recurso2);
    return NULL;
}

int main() {
    pthread_t hilo1, hilo2;

    // Inicializar mutex
    pthread_mutex_init(&mutex_recurso1, NULL);
    pthread_mutex_init(&mutex_recurso2, NULL);

    printf("Iniciando simulación de Deadlock...\n");
    
    // Creación de hilos
    pthread_create(&hilo1, NULL, rutina_hilo1, NULL);
    pthread_create(&hilo2, NULL, rutina_hilo2, NULL);

    // Esperar a que los hilos finalicen
    pthread_join(hilo1, NULL);
    pthread_join(hilo2, NULL);

    printf("Programa finalizado correctamente.\n");

    // Destruir mutex
    pthread_mutex_destroy(&mutex_recurso1);
    pthread_mutex_destroy(&mutex_recurso2);
    return 0;
}