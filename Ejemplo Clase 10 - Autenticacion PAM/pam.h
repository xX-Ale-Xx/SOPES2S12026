#ifndef PAM_H
#define PAM_H

int pam_conv_callback(
    int num_msg,
    const struct pam_message **msg,
    struct pam_response **resp,
    void *appdata_ptr
);

int user_in_group(const char *user, const char *group);

int pam_auth_check(const char *username, const char *password);

#endif
