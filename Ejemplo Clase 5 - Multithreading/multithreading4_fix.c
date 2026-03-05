#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define THREAD_COUNT 5

/* 1. Declaramos el Mutex de forma global */
pthread_mutex_t lock;

void* myFunction(void *arg) {
    int n = *(int*)arg;
    static int x = 10;

    /* 2. Bloqueamos el acceso (Lock) */
    /* A partir de aquí, solo UN hilo puede ejecutar este bloque a la vez */
    pthread_mutex_lock(&lock);

    int y = x;
    printf("Thread %d: x = %d, y = %d\n", n, x, y);

    if (x != y) {
        printf("Thread %d: Error! x != y! %d != %d\n", n, x, y);
    }

    x++;

    /* 3. Liberamos el acceso (Unlock) */
    pthread_mutex_unlock(&lock);

    free(arg); 
    return 0;
}

int main(void) {
    pthread_t thread[THREAD_COUNT];

    /* 4. Inicializamos el Mutex */
    if (pthread_mutex_init(&lock, NULL) != 0) {
        printf("Error al inicializar el mutex\n");
        return 1;
    }

    for (int i = 0; i < THREAD_COUNT; i++) {
        int *n = malloc(sizeof *n);
        *n = i;
        pthread_create(&thread[i], NULL, myFunction, (void *) n);
    }

    for (int i = 0; i < THREAD_COUNT; i++) {
        pthread_join(thread[i], NULL);
    }

    /* 5. Destruimos el Mutex al finalizar */
    pthread_mutex_destroy(&lock);

    printf("Fin del programa. Valor final de x: (revisa el output)\n");
    return 0;
}