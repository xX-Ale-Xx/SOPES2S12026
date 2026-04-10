#include <security/pam_appl.h>
#include <security/pam_misc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include "pam.h"

// callback PAM: responde prompt de contraseña usando appdata_ptr
int pam_conv_callback(
    int num_msg,
    const struct pam_message **msg,
    struct pam_response **resp,
    void *appdata_ptr
)
{
    if (num_msg <= 0) return PAM_CONV_ERR;
    struct pam_response *responses = calloc(num_msg, sizeof(struct pam_response));
    if (!responses) return PAM_CONV_ERR;

    const char *password = (const char *)appdata_ptr;
    for (int i = 0; i < num_msg; ++i) {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF) {
            responses[i].resp = strdup(password);
        } else {
            responses[i].resp = NULL;
        }
    }

    *resp = responses;
    return PAM_SUCCESS;
}

// comprueba si 'user' pertenece al grupo 'group'
int user_in_group(const char *user, const char *group)
{
    if (!user || !group) return 0;
    struct passwd *pw = getpwnam(user);
    if (!pw) return 0;

    struct group *gr = getgrnam(group);
    if (!gr) return 0;

    /* primary gid */
    if ((gid_t)pw->pw_gid == gr->gr_gid) return 1;

    /* check supplementary list in group struct */
    for (char **m = gr->gr_mem; m && *m; ++m) {
        if (strcmp(*m, user) == 0) return 1;
    }
    return 0;
}

/*
pam_auth_check: Autentica con PAM y devuelve rol:
return:
    0  => autenticación exitosa
    >0 => retorna código PAM (pam_* return)
    -1 => error local
*/
int pam_auth_check(const char *username, const char *password)
{
    if (!username || !password ) return -1;

    char *pwdup = strdup(password);
    if (!pwdup) return -1;

    struct pam_conv conv = { pam_conv_callback, pwdup };
    pam_handle_t *pamh = NULL;
    int rc = pam_start("login", username, &conv, &pamh);
    if (rc != PAM_SUCCESS) {
        free(pwdup);
        return rc;
    }

    rc = pam_authenticate(pamh, 0);
    if (rc == PAM_SUCCESS) rc = pam_acct_mgmt(pamh, 0);

    pam_end(pamh, rc);
    free(pwdup);
    return rc;
}