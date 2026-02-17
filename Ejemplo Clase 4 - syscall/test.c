#include <stdlib.h>
#include <sys/syscall.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>

struct ram_info {
    unsigned long total;
    unsigned long used;
    unsigned long free;
    unsigned long cache;
    unsigned int mem_unit;
} ;

int main() {
    struct ram_info info;
    long ret = syscall(600, &info);

    if (ret < 0) {
        perror("syscall");
        return EXIT_FAILURE;
    }

    printf("RAM Total: %.2f MB\n", (double)info.total / (1024.0 * 1024.0));
    printf("RAM Usada: %.2f MB\n", (double)info.used / (1024.0 * 1024.0));
    printf("RAM Libre: %.2f MB\n", (double)info.free / (1024.0 * 1024.0));
    printf("RAM Cache: %.2f MB\n", (double)info.cache / (1024.0 * 1024.0));
    printf("Unidad de Memoria: %u bytes\n", info.mem_unit);

    return EXIT_SUCCESS;
}