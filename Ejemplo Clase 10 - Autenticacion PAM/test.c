#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pam.h"

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "uso: %s usuario contraseña grupo\n", argv[0]);
        return 2;
    }
    int rc = pam_auth_check(argv[1], argv[2]);
    printf("pam_auth_check rc=%d\n", rc);

    if (rc == 0) {
        printf("Autenticación exitosa.");
    } else if (rc > 0) {
        printf("Autenticación fallida. Código PAM: %d\n", rc);
    } else {
        printf("Error local durante la autenticación.\n");
    }

    int in_group = user_in_group(argv[1], argv[3]);

    if (in_group) {
        printf("El usuario '%s' pertenece al grupo '%s'.\n", argv[1], argv[3]);
    } else {
        printf("El usuario '%s' NO pertenece al grupo '%s'.\n", argv[1], argv[3]);
    }

    return 0;
}
