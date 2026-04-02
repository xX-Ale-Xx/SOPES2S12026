/*
 * daemon.c
 * =====
 * Programa intermedio (daemon) que:
 *   1. Autentica al usuario via PAM antes de arrancar.
 *   2. Lee /proc/file_analyzer para obtener hash, tamaño y timestamp.
 *   3. Mantiene un registro de archivos analizados (hash + timestamp).
 *   4. Detecta archivos NUEVOS, MODIFICADOS o SIN CAMBIOS.
 *   5. Genera alertas con severidad LOW / MEDIUM / HIGH.
 *   6. Envía todo como JSON por WebSocket usando libwebsockets.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE   700

#include <libwebsockets.h>
#include <security/pam_appl.h>
#include <security/pam_misc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>
#include <grp.h>
#include <pwd.h>

/* ---- Configuración ---- */
#define WS_PORT          8081
#define JSON_BUF         65536
#define PROC_PATH        "/proc/file_analyzer"
#define PROC_CMD_PATH    "/proc/file_analyzer_cmd"
#define PROC_MEM_PATH    "/proc/mem_monitor"
#define MONITOR_DIR      "/home/user/monitor"
#define HASH_DB_DIR      "bd"
#define HASH_DB_NAME     "file_analyzer_hashes.db"
#define SCAN_INTERVAL_S  5        /* segundos entre escaneos */
#define MAX_FILES        256
#define MAX_PATH_LEN     512
#define SHA256_HEX_LEN   64
#define MAX_TOP_PROCS    16

/* ---- Grupos PAM para control de roles ---- */
#define GROUP_ADMIN  "admin_user"
#define GROUP_COMMON "common_user"

/* ---- Severidades ---- */
typedef enum { SEV_LOW = 0, SEV_MEDIUM, SEV_HIGH } severity_t;

static const char *sev_str[] = { "LOW", "MEDIUM", "HIGH" };
static char g_monitor_dir[MAX_PATH_LEN] = MONITOR_DIR;
static char g_hash_db_path[MAX_PATH_LEN] = "";

/* ---- Registro de un archivo monitoreado ---- */
typedef struct {
    char path[MAX_PATH_LEN];
    char sha256[SHA256_HEX_LEN + 1];
    long mtime;
    long size;
    int  is_new;       /* Primera vez que se ve */
    int  is_deleted;   /* 1 si ya no existe en el directorio */
    int  seen;         /* 1 si apareció en el escaneo actual */
} file_record_t;

/* Base de datos en memoria de archivos conocidos */
static file_record_t  g_known_files[MAX_FILES];
static int            g_known_count = 0;
static pthread_mutex_t g_files_mutex = PTHREAD_MUTEX_INITIALIZER;

static void sleep_ms(long ms)
{
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void init_paths(void)
{
    const char *home = NULL;
    const char *sudo_user = getenv("SUDO_USER");
    const char *env_home = getenv("HOME");
    struct passwd *pw;

    /* Si se ejecuta con sudo, usar el HOME del usuario real, no el de root */
    if (sudo_user && sudo_user[0]) {
        pw = getpwnam(sudo_user);
        if (pw && pw->pw_dir && pw->pw_dir[0])
            home = pw->pw_dir;
    }

    if (!home || !home[0])
        home = env_home;

    if (!home || !home[0]) {
        pw = getpwuid(getuid());
        if (pw && pw->pw_dir && pw->pw_dir[0])
            home = pw->pw_dir;
    }

    if (home && home[0]) {
        snprintf(g_monitor_dir, sizeof(g_monitor_dir), "%s/monitor", home);
        snprintf(g_hash_db_path, sizeof(g_hash_db_path), "%s/%s", HASH_DB_DIR, HASH_DB_NAME);
    } else {
        strncpy(g_monitor_dir, MONITOR_DIR, sizeof(g_monitor_dir) - 1);
        g_monitor_dir[sizeof(g_monitor_dir) - 1] = '\0';
        snprintf(g_hash_db_path, sizeof(g_hash_db_path), "%s/%s", HASH_DB_DIR, HASH_DB_NAME);
    }
}

static int touch_file(const char *path)
{
    FILE *fp;

    if (!path || !path[0])
        return -1;

    fp = fopen(path, "a");
    if (!fp)
        return -1;

    fclose(fp);
    return 0;
}

static void load_known_files_db(void)
{
    FILE *fp;
    char line[1024];

    if (touch_file(g_hash_db_path) != 0)
        return;

    fp = fopen(g_hash_db_path, "r");
    if (!fp)
        return;

    pthread_mutex_lock(&g_files_mutex);
    g_known_count = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        file_record_t rec;
        char path[MAX_PATH_LEN];
        char sha[SHA256_HEX_LEN + 1];
        long mtime = 0;
        long size = 0;
        int is_new = 0;
        int is_deleted = 0;

        memset(&rec, 0, sizeof(rec));

         if (sscanf(line, "%511[^|]|%64[^|]|%ld|%ld|%d|%d",
                 path, sha, &mtime, &size, &is_new, &is_deleted) < 5)
             continue;

        strncpy(rec.path, path, sizeof(rec.path) - 1);
        strncpy(rec.sha256, sha, sizeof(rec.sha256) - 1);
        rec.mtime = mtime;
        rec.size = size;
        rec.is_new = is_new;
        rec.is_deleted = is_deleted;
        rec.seen = 0;

        if (g_known_count < MAX_FILES)
            g_known_files[g_known_count++] = rec;
    }

    pthread_mutex_unlock(&g_files_mutex);
    fclose(fp);
}

static void save_known_files_db(void)
{
    FILE *fp;
    int i;

    fp = fopen(g_hash_db_path, "w");
    if (!fp)
        return;

    pthread_mutex_lock(&g_files_mutex);
    for (i = 0; i < g_known_count; i++) {
        file_record_t *rec = &g_known_files[i];
        fprintf(fp, "%s|%s|%ld|%ld|%d|%d\n",
            rec->path, rec->sha256, rec->mtime, rec->size,
            rec->is_new, rec->is_deleted);
    }
    pthread_mutex_unlock(&g_files_mutex);

    fclose(fp);
}

static int mkdir_p(const char *path)
{
    char tmp[MAX_PATH_LEN];
    size_t len;
    char *p;

    if (!path || !path[0])
        return -1;

    if (snprintf(tmp, sizeof(tmp), "%s", path) >= (int)sizeof(tmp))
        return -1;

    len = strlen(tmp);
    if (len == 0)
        return -1;

    if (tmp[len - 1] == '/')
        tmp[len - 1] = '\0';

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;

    return 0;
}

/* ---- Una alerta generada por el daemon ---- */
typedef struct {
    char        event_type[32];  /* file, memory, process */
    char        description[256];
    severity_t  severity;
    char        file_path[MAX_PATH_LEN];
    time_t      timestamp;
} alert_t;

/* ---- Datos de memoria del módulo mem_monitor ---- */
typedef struct {
    unsigned long total_kb;
    unsigned long used_kb;
    unsigned long free_kb;
    unsigned long cached_kb;
    unsigned long swap_used_kb;
    unsigned long active_kb;
    unsigned long inactive_kb;
    unsigned long minor_faults;
    unsigned long major_faults;
    int top_count;
    struct {
        int pid;
        char name[64];
        unsigned long rss_kb;
        unsigned long pct_x100;
    } top[MAX_TOP_PROCS];
} mem_data_t;

/* ---- Estado global ---- */
static volatile sig_atomic_t  g_running     = 1;
static char                   g_latest_json[JSON_BUF] = "{\"status\":\"initializing\"}";
static pthread_mutex_t        g_json_mutex  = PTHREAD_MUTEX_INITIALIZER;
static const struct lws_protocols *g_proto  = NULL;

/* Cola sencilla de alertas (circular, últimas 64) */
#define MAX_ALERTS 64
static alert_t  g_alerts[MAX_ALERTS];
static int      g_alert_count = 0;
static pthread_mutex_t g_alert_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Rol del usuario autenticado */
typedef enum { ROLE_NONE, ROLE_COMMON, ROLE_ADMIN } user_role_t;
static user_role_t g_role = ROLE_NONE;
static char        g_username[64] = "";
static pthread_mutex_t g_auth_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ---- Datos de memoria (publicados por WS) ---- */
static mem_data_t g_mem_data;
static pthread_mutex_t g_mem_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int authenticated;
    int sent_auth_prompt;
    int auth_ok_pending;
    user_role_t role;
    char username[64];
} session_data_t;

typedef struct {
    const char *username;
    const char *password;
} pam_auth_data_t;

/* 
 * PAM: Autenticación de usuarios
*/

/*
 * pam_conversation()
 * Callback de PAM para obtener credenciales.
 */
static int pam_conversation(int num_msg, const struct pam_message **msg,
                             struct pam_response **resp, void *appdata_ptr)
{
    const pam_auth_data_t *auth = (const pam_auth_data_t *)appdata_ptr;
    struct pam_response *reply;
    int i;

    reply = calloc(num_msg, sizeof(struct pam_response));
    if (!reply)
        return PAM_BUF_ERR;

    for (i = 0; i < num_msg; i++) {
        switch (msg[i]->msg_style) {
        case PAM_PROMPT_ECHO_OFF:   /* Solicitud de contraseña */
            reply[i].resp = strdup(auth && auth->password ? auth->password : "");
            reply[i].resp_retcode = 0;
            break;
        case PAM_PROMPT_ECHO_ON:    /* Solicitud de usuario (si la hubiera) */
            reply[i].resp = strdup(auth && auth->username ? auth->username : "");
            reply[i].resp_retcode = 0;
            break;
        default:
            /* PAM_ERROR_MSG, PAM_TEXT_INFO, etc. — ignorar */
            reply[i].resp = NULL;
            break;
        }
    }

    *resp = reply;
    return PAM_SUCCESS;
}

/*
 * check_user_in_group()
 * Verifica si un usuario pertenece a un grupo del sistema.
 * Retorna 1 si pertenece, 0 si no.
 */
static int check_user_in_group(const char *username, const char *groupname)
{
    struct group *grp;
    char **member;

    grp = getgrnam(groupname);
    if (!grp)
        return 0;

    for (member = grp->gr_mem; *member != NULL; member++) {
        if (strcmp(*member, username) == 0)
            return 1;
    }
    return 0;
}

/*
 * authenticate_user()
 * Autentica con PAM y determina el rol del usuario.
 * Retorna el rol: ROLE_ADMIN, ROLE_COMMON, o ROLE_NONE si falla.
 */
static user_role_t authenticate_user(const char *username, const char *password)
{
    pam_handle_t *pamh = NULL;
    pam_auth_data_t auth_data = { username, password };
    struct pam_conv conv = { pam_conversation, (void *)&auth_data };
    int ret;
    user_role_t role = ROLE_NONE;

    /* Abrir sesión PAM con el servicio definido para este flujo */
    ret = pam_start("sudo", username, &conv, &pamh);
    if (ret != PAM_SUCCESS) {
        fprintf(stderr, "[PAM] pam_start fallo: %s\n", pam_strerror(pamh, ret));
        return ROLE_NONE;
    }

    /* Autenticar credenciales */
    ret = pam_authenticate(pamh, 0);
    if (ret != PAM_SUCCESS) {
        fprintf(stderr, "[PAM] Autenticacion fallida para '%s': %s\n",
                username, pam_strerror(pamh, ret));
        pam_end(pamh, ret);
        return ROLE_NONE;
    }

    /* Verificar que la cuenta está activa (permite fallos leves) */
    ret = pam_acct_mgmt(pamh, PAM_SILENT);
    if (ret != PAM_SUCCESS && ret != PAM_NEW_AUTHTOK_REQD) {
        fprintf(stderr, "[PAM] Advertencia de cuenta para '%s': %s (continuando)\n",
                username, pam_strerror(pamh, ret));
    }

    pam_end(pamh, PAM_SUCCESS);

    /*
     * Determinar rol verificando membresía de grupos.
     * Si pertenece a ambos grupos, admin tiene prioridad.
     */
    int is_admin  = check_user_in_group(username, GROUP_ADMIN);
    int is_common = check_user_in_group(username, GROUP_COMMON);

    if (is_admin)
        role = ROLE_ADMIN;
    else if (is_common)
        role = ROLE_COMMON;
    else
        role = ROLE_NONE;   /* Autenticó pero no tiene rol válido */

    return role;
}

static void set_active_user(const char *username, user_role_t role)
{
    pthread_mutex_lock(&g_auth_mutex);
    strncpy(g_username, username, sizeof(g_username) - 1);
    g_username[sizeof(g_username) - 1] = '\0';
    g_role = role;
    pthread_mutex_unlock(&g_auth_mutex);
}

static void get_active_user(char *username_out, size_t username_sz, user_role_t *role_out)
{
    pthread_mutex_lock(&g_auth_mutex);
    if (username_out && username_sz > 0) {
        strncpy(username_out, g_username, username_sz - 1);
        username_out[username_sz - 1] = '\0';
    }
    if (role_out)
        *role_out = g_role;
    pthread_mutex_unlock(&g_auth_mutex);
}

static int copy_ws_payload(char *dst, size_t dst_sz, const void *in, size_t len)
{
    size_t n;

    if (dst_sz == 0)
        return -1;

    n = len < (dst_sz - 1) ? len : (dst_sz - 1);
    memcpy(dst, in, n);
    dst[n] = '\0';
    return 0;
}

static const char *json_find_string(const char *json, const char *key, char *out, size_t out_sz)
{
    const char *p = strstr(json, key);
    size_t i = 0;

    if (!p)
        return NULL;

    p = strchr(p, ':');
    if (!p)
        return NULL;

    p++;
    while (*p && isspace((unsigned char)*p))
        p++;

    if (*p != '"')
        return NULL;
    p++;

    while (*p && *p != '"' && i + 1 < out_sz) {
        if (*p == '\\' && p[1] != '\0')
            p++;
        out[i++] = *p++;
    }

    out[i] = '\0';
    return (*p == '"') ? out : NULL;
}

static int send_ws_text(struct lws *wsi, const char *text)
{
    size_t len = strlen(text);
    unsigned char *buf = malloc(LWS_PRE + len);

    if (!buf)
        return -1;

    memcpy(&buf[LWS_PRE], text, len);
    if (lws_write(wsi, &buf[LWS_PRE], len, LWS_WRITE_TEXT) < 0) {
        free(buf);
        return -1;
    }

    free(buf);
    return 0;
}

/* 
 * Gestión de alertas
 */

static void add_alert(const char *event_type, const char *description,
                      severity_t sev, const char *file_path)
{
    alert_t *a;

    pthread_mutex_lock(&g_alert_mutex);

    /* Circular: si está lleno, sobreescribir el más antiguo */
    if (g_alert_count < MAX_ALERTS)
        a = &g_alerts[g_alert_count++];
    else {
        /* Desplazar para mantener los más recientes */
        memmove(&g_alerts[0], &g_alerts[1],
                sizeof(alert_t) * (MAX_ALERTS - 1));
        a = &g_alerts[MAX_ALERTS - 1];
    }

    strncpy(a->event_type,  event_type,  sizeof(a->event_type)  - 1);
    strncpy(a->description, description, sizeof(a->description)  - 1);
    strncpy(a->file_path,   file_path,   sizeof(a->file_path)    - 1);
    a->severity  = sev;
    a->timestamp = time(NULL);

    pthread_mutex_unlock(&g_alert_mutex);

    printf("[ALERTA][%s] %s - %s\n", sev_str[sev], event_type, description);
}

/* 
 * Lectura de /proc/file_analyzer
  */

typedef struct {
    char status[16];
    char path[MAX_PATH_LEN];
    long long size_bytes;
    long mtime;
    char sha256[SHA256_HEX_LEN + 1];
    int  changed;
} proc_result_t;

/*
 * trigger_analysis()
 * Escribe la ruta en /proc/file_analyzer_cmd para que el módulo
 * calcule el hash. Equivale a invocar una syscall
 */
static int trigger_analysis(const char *path)
{
    FILE *fp = fopen(PROC_CMD_PATH, "w");
    if (!fp) {
        perror("fopen /proc/file_analyzer_cmd");
        return -1;
    }
    fprintf(fp, "%s\n", path);
    fclose(fp);
    return 0;
}

/*
 * read_proc_result()
 * Lee /proc/file_analyzer y parsea el resultado.
 */
static int read_proc_result(proc_result_t *out)
{
    FILE  *fp;
    char   line[512];

    memset(out, 0, sizeof(*out));
    strncpy(out->status, "unknown", sizeof(out->status) - 1);

    fp = fopen(PROC_PATH, "r");
    if (!fp)
        return -1;

    while (fgets(line, sizeof(line), fp)) {
        /* Quitar newline */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        if (sscanf(line, "status=%15s",  out->status) == 1) continue;
        if (sscanf(line, "path=%511s",   out->path)   == 1) continue;
        if (sscanf(line, "size_bytes=%lld", &out->size_bytes) == 1) continue;
        if (sscanf(line, "mtime=%ld",    &out->mtime)  == 1) continue;
        if (sscanf(line, "sha256=%64s",  out->sha256)  == 1) continue;
        if (sscanf(line, "changed=%d",   &out->changed)== 1) continue;
    }

    fclose(fp);
    return (strcmp(out->status, "ok") == 0) ? 0 : -1;
}

/* 
 * Lectura de /proc/mem_monitor - Datos de memoria
 *  */

static int read_mem_data(mem_data_t *out)
{
    FILE  *fp;
    char   line[512];
    int    idx = 0;

    memset(out, 0, sizeof(*out));

    fp = fopen(PROC_MEM_PATH, "r");
    if (!fp) {
        fprintf(stderr, "[MEM] No se pudo leer %s\n", PROC_MEM_PATH);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        unsigned long v = 0;
        int pid;
        char name[64];
        unsigned long rss;
        unsigned long pct_x100;

        /* Parsear líneas key=value */
        if (sscanf(line, "total_kb=%lu", &v) == 1) {
            out->total_kb = v;
            continue;
        }
        if (sscanf(line, "used_kb=%lu", &v) == 1) {
            out->used_kb = v;
            continue;
        }
        if (sscanf(line, "free_kb=%lu", &v) == 1) {
            out->free_kb = v;
            continue;
        }
        if (sscanf(line, "cached_kb=%lu", &v) == 1) {
            out->cached_kb = v;
            continue;
        }
        if (sscanf(line, "swap_used_kb=%lu", &v) == 1) {
            out->swap_used_kb = v;
            continue;
        }
        if (sscanf(line, "active_kb=%lu", &v) == 1) {
            out->active_kb = v;
            continue;
        }
        if (sscanf(line, "inactive_kb=%lu", &v) == 1) {
            out->inactive_kb = v;
            continue;
        }
        if (sscanf(line, "minor_faults=%lu", &v) == 1) {
            out->minor_faults = v;
            continue;
        }
        if (sscanf(line, "major_faults=%lu", &v) == 1) {
            out->major_faults = v;
            continue;
        }

        /* Procesos top: proc=pid|name|rss_kb|pct_x100 */
        if (idx < MAX_TOP_PROCS &&
            sscanf(line, "proc=%d|%63[^|]|%lu|%lu", &pid, name, &rss, &pct_x100) == 4) {
            out->top[idx].pid = pid;
            snprintf(out->top[idx].name, sizeof(out->top[idx].name), "%s", name);
            out->top[idx].rss_kb = rss;
            out->top[idx].pct_x100 = pct_x100;
            idx++;
        }
    }

    fclose(fp);
    out->top_count = idx;

    /* Fallback si no llegó total_kb */
    if (out->total_kb == 0) {
        unsigned long approx = out->used_kb + out->free_kb + out->cached_kb;
        out->total_kb = approx;
    }

    return 0;
}

static void get_mem_data(mem_data_t *out)
{
    pthread_mutex_lock(&g_mem_mutex);
    memcpy(out, &g_mem_data, sizeof(mem_data_t));
    pthread_mutex_unlock(&g_mem_mutex);
}

/* 
 * Thread 1: Monitoreo de memoria (lee /proc/mem_monitor)
 *  */

static void *thread_mem_monitor(void *arg)
{
    (void)arg;
    printf("[THREAD-MEM] Iniciado. Leyendo de: %s\n", PROC_MEM_PATH);

    while (g_running) {
        mem_data_t data;

        if (read_mem_data(&data) == 0) {
            pthread_mutex_lock(&g_mem_mutex);
            memcpy(&g_mem_data, &data, sizeof(mem_data_t));
            pthread_mutex_unlock(&g_mem_mutex);
        }

        sleep(1);  /* Actualizar cada 1 segundo */
    }

    printf("[THREAD-MEM] Terminado.\n");
    return NULL;
}

/* 
 * Thread 2: Escaneo de archivos
 *  */

/*
 * find_known_file()
 * Busca un archivo en la base de datos interna por su ruta.
 * Retorna índice o -1 si no existe.
 */
static int find_known_file(const char *path)
{
    int i;
    for (i = 0; i < g_known_count; i++) {
        if (strcmp(g_known_files[i].path, path) == 0)
            return i;
    }
    return -1;
}

/*
 * scan_directory()
 * Recorre MONITOR_DIR, dispara análisis de cada archivo,
 * y genera alertas según el estado detectado.
 *
 * Severidades aplicadas:
 *   - Archivo nuevo          - LOW
 *   - Archivo modificado     - MEDIUM
 *   - Sin cambios            - (silencioso, actualiza registro)
 */
static void scan_directory(void)
{
    DIR           *dir;
    struct dirent *entry;
    char           full_path[MAX_PATH_LEN];
    proc_result_t  result;
    char           desc[256];
    int            i;

    pthread_mutex_lock(&g_files_mutex);
    for (i = 0; i < g_known_count; i++)
        g_known_files[i].seen = 0;
    pthread_mutex_unlock(&g_files_mutex);

    dir = opendir(g_monitor_dir);
    if (!dir) {
        perror("[SCAN] opendir");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        /* Ignorar . y .. */
        if (entry->d_name[0] == '.')
            continue;

        snprintf(full_path, sizeof(full_path), "%s/%s",
                 g_monitor_dir, entry->d_name);

        /* Disparar análisis en el módulo kernel */
        if (trigger_analysis(full_path) != 0)
            continue;

        /* Pequeña pausa para que el módulo procese */
        sleep_ms(50);

        /* Leer resultado del módulo */
        if (read_proc_result(&result) != 0)
            continue;

        pthread_mutex_lock(&g_files_mutex);

        int idx = find_known_file(full_path);

        if (idx == -1) {
            /* === Archivo NUEVO === */
            if (g_known_count < MAX_FILES) {
                file_record_t *rec = &g_known_files[g_known_count++];
                strncpy(rec->path,   result.path,   MAX_PATH_LEN - 1);
                strncpy(rec->sha256, result.sha256, SHA256_HEX_LEN);
                rec->mtime  = result.mtime;
                rec->size   = result.size_bytes;
                rec->is_new = 1;
                rec->is_deleted = 0;
                rec->seen = 1;

                snprintf(desc, sizeof(desc),
                         "Nuevo archivo detectado: %s (size=%lld bytes)",
                         entry->d_name, result.size_bytes);
                pthread_mutex_unlock(&g_files_mutex);

                /* Severidad LOW para archivos nuevos */
                add_alert("file", desc, SEV_LOW, full_path);
            } else {
                pthread_mutex_unlock(&g_files_mutex);
            }

        } else {
            /* Archivo ya conocido: verificar si cambió */
            file_record_t *rec = &g_known_files[idx];
            rec->seen = 1;
            rec->is_deleted = 0;
            int hash_changed = (strncmp(rec->sha256, result.sha256,
                                        SHA256_HEX_LEN) != 0);

            if (hash_changed) {
                /* === Archivo MODIFICADO === */
                snprintf(desc, sizeof(desc),
                         "Archivo modificado: %s | SHA anterior: %.16s... | SHA actual: %.16s...",
                         entry->d_name, rec->sha256, result.sha256);

                /* Actualizar registro */
                strncpy(rec->sha256, result.sha256, SHA256_HEX_LEN);
                rec->mtime = result.mtime;
                rec->size  = result.size_bytes;
                rec->is_new = 0;

                pthread_mutex_unlock(&g_files_mutex);

                /* Severidad MEDIUM para modificaciones */
                add_alert("file", desc, SEV_MEDIUM, full_path);
            } else {
                /* Sin cambios — solo actualizar timestamp */
                rec->mtime = result.mtime;
                rec->is_new = 0;
                pthread_mutex_unlock(&g_files_mutex);
            }
        }
    }

    for (i = 0; i < g_known_count; i++) {
        file_record_t *rec = &g_known_files[i];

        if (!rec->seen && !rec->is_deleted) {
            char deleted_desc[256];

            rec->is_deleted = 1;
            snprintf(deleted_desc, sizeof(deleted_desc),
                     "Archivo eliminado: %s", rec->path);
            pthread_mutex_unlock(&g_files_mutex);
            add_alert("file", deleted_desc, SEV_HIGH, rec->path);
            pthread_mutex_lock(&g_files_mutex);
        }
    }

    closedir(dir);
    pthread_mutex_unlock(&g_files_mutex);
}

static void *thread_file_scan(void *arg)
{
    (void)arg;
    printf("[THREAD-SCAN] Iniciado. Monitoreando: %s\n", g_monitor_dir);

    while (g_running) {
        scan_directory();
        save_known_files_db();
        sleep(SCAN_INTERVAL_S);
    }

    save_known_files_db();

    printf("[THREAD-SCAN] Terminado.\n");
    return NULL;
}

/* 
 * Construcción del JSON
 *  */

static void build_and_publish_json(struct lws_context *ctx)
{
    char    buf[JSON_BUF];
    size_t  off = 0;
    time_t  now = time(NULL);
    struct tm tm_utc;
    char    ts[64];
    char    active_user[64] = "";
    int     i;
    user_role_t active_role = ROLE_NONE;
    mem_data_t  mem_data;

    gmtime_r(&now, &tm_utc);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

    get_active_user(active_user, sizeof(active_user), &active_role);
    get_mem_data(&mem_data);

    /* Encabezado: metadatos del daemon */
    off += snprintf(buf + off, sizeof(buf) - off,
        "{"
        "\"timestamp\":\"%s\","
        "\"daemon_user\":\"%s\","
        "\"daemon_role\":\"%s\","
        "\"monitor_dir\":\"%s\",",
        ts,
        active_user,
        active_role == ROLE_ADMIN ? "admin" : (active_role == ROLE_COMMON ? "common" : "none"),
        g_monitor_dir);

    /* === Datos de Memoria === */
    off += snprintf(buf + off, sizeof(buf) - off,
        "\"memory\":{"
        "\"total_kb\":%lu,"
        "\"used_kb\":%lu,"
        "\"free_kb\":%lu,"
        "\"cached_kb\":%lu,"
        "\"swap_used_kb\":%lu,"
        "\"active_kb\":%lu,"
        "\"inactive_kb\":%lu"
        "},",
        mem_data.total_kb,
        mem_data.used_kb,
        mem_data.free_kb,
        mem_data.cached_kb,
        mem_data.swap_used_kb,
        mem_data.active_kb,
        mem_data.inactive_kb);

    /* === Fallos de página === */
    off += snprintf(buf + off, sizeof(buf) - off,
        "\"page_faults\":{"
        "\"minor\":%lu,"
        "\"major\":%lu"
        "},",
        mem_data.minor_faults,
        mem_data.major_faults);

    /* === Procesos (top) === */
    off += snprintf(buf + off, sizeof(buf) - off, "\"top_processes\":[");
    for (i = 0; i < mem_data.top_count && off < sizeof(buf) - 300; i++) {
        unsigned long pct_int = mem_data.top[i].pct_x100 / 100;
        unsigned long pct_dec = mem_data.top[i].pct_x100 % 100;

        off += snprintf(buf + off, sizeof(buf) - off,
            "%s{"
            "\"pid\":%d,"
            "\"name\":\"%s\","
            "\"rss_kb\":%lu,"
            "\"mem_percent\":%lu.%02lu"
            "}",
            (i == 0) ? "" : ",",
            mem_data.top[i].pid,
            mem_data.top[i].name,
            mem_data.top[i].rss_kb,
            pct_int, pct_dec);
    }
    off += snprintf(buf + off, sizeof(buf) - off, "],");

    /* === Archivos analizados === */
    off += snprintf(buf + off, sizeof(buf) - off, "\"files\":[");

    pthread_mutex_lock(&g_files_mutex);
    for (i = 0; i < g_known_count && off < sizeof(buf) - 200; i++) {
        file_record_t *r = &g_known_files[i];
        off += snprintf(buf + off, sizeof(buf) - off,
            "%s{"
            "\"path\":\"%s\","
            "\"sha256\":\"%.16s...\","   /* Mostrar solo prefijo en el JSON */
            "\"sha256_full\":\"%s\","
            "\"mtime\":%ld,"
            "\"size_bytes\":%ld,"
            "\"is_new\":%s," 
            "\"deleted\":%s"
            "}",
            (i == 0) ? "" : ",",
            r->path, r->sha256, r->sha256,
            r->mtime, r->size,
            r->is_new ? "true" : "false",
            r->is_deleted ? "true" : "false");
    }
    pthread_mutex_unlock(&g_files_mutex);

    off += snprintf(buf + off, sizeof(buf) - off, "],");

    /* === Alertas (más recientes primero) === */
    off += snprintf(buf + off, sizeof(buf) - off, "\"alerts\":[");

    pthread_mutex_lock(&g_alert_mutex);
    /* Recorrer en orden inverso para poner las más recientes primero */
    int first = 1;
    for (i = g_alert_count - 1; i >= 0 && off < sizeof(buf) - 300; i--) {
        alert_t *a = &g_alerts[i];
        char alert_ts[64];
        struct tm atm;
        gmtime_r(&a->timestamp, &atm);
        strftime(alert_ts, sizeof(alert_ts), "%Y-%m-%dT%H:%M:%SZ", &atm);

        off += snprintf(buf + off, sizeof(buf) - off,
            "%s{"
            "\"event_type\":\"%s\","
            "\"description\":\"%s\","
            "\"severity\":\"%s\","
            "\"file\":\"%s\","
            "\"timestamp\":\"%s\""
            "}",
            first ? "" : ",",
            a->event_type,
            a->description,
            sev_str[a->severity],
            a->file_path,
            alert_ts);
        first = 0;
    }
    pthread_mutex_unlock(&g_alert_mutex);

    off += snprintf(buf + off, sizeof(buf) - off, "]}");

    /* Publicar JSON de forma thread-safe */
    pthread_mutex_lock(&g_json_mutex);
    strncpy(g_latest_json, buf, JSON_BUF - 1);
    pthread_mutex_unlock(&g_json_mutex);

    /* Notificar a todos los clientes WS que hay datos nuevos */
    if (ctx && g_proto)
        lws_callback_on_writable_all_protocol(ctx, g_proto);
}

/* 
 * WebSocket server 
 *  */

static int callback_file_analyzer(struct lws *wsi,
                                   enum lws_callback_reasons reason,
                                   void *user, void *in, size_t len)
{
    session_data_t *session = (session_data_t *)user;

    (void)in; (void)len;

    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
        printf("[WS] Cliente conectado\n");
        session->authenticated = 0;
        session->sent_auth_prompt = 0;
        session->auth_ok_pending = 0;
        session->role = ROLE_NONE;
        session->username[0] = '\0';
        lws_callback_on_writable(wsi);
        break;

    case LWS_CALLBACK_RECEIVE: {
        char msg[1024];
        char type[32];
        char username[64];
        char password[128];
        user_role_t role;

        if (copy_ws_payload(msg, sizeof(msg), in, len) != 0)
            return -1;

        if (session->authenticated)
            break;

        if (!json_find_string(msg, "\"type\"", type, sizeof(type)) ||
            strcmp(type, "auth") != 0) {
            send_ws_text(wsi, "{\"type\":\"error\",\"message\":\"auth_required\"}");
            lws_callback_on_writable(wsi);
            return 0;
        }

        if (!json_find_string(msg, "\"username\"", username, sizeof(username)) ||
            !json_find_string(msg, "\"password\"", password, sizeof(password))) {
            send_ws_text(wsi, "{\"type\":\"auth\",\"ok\":false,\"error\":\"invalid_payload\"}");
            lws_callback_on_writable(wsi);
            return 0;
        }

        role = authenticate_user(username, password);
        if (role == ROLE_NONE) {
            send_ws_text(wsi, "{\"type\":\"auth\",\"ok\":false,\"error\":\"invalid_credentials\"}");
            lws_callback_on_writable(wsi);
            return 0;
        }

        session->authenticated = 1;
        session->auth_ok_pending = 1;
        session->role = role;
        strncpy(session->username, username, sizeof(session->username) - 1);
        session->username[sizeof(session->username) - 1] = '\0';
        set_active_user(username, role);
        lws_callback_on_writable(wsi);
        break;
    }

    case LWS_CALLBACK_SERVER_WRITEABLE:
        if (!session->authenticated) {
            if (!session->sent_auth_prompt) {
                session->sent_auth_prompt = 1;
                return send_ws_text(wsi,
                    "{\"type\":\"auth_required\",\"message\":\"send username and password\"}");
            }
            return 0;
        }

        if (session->auth_ok_pending) {
            session->auth_ok_pending = 0;
            if (send_ws_text(wsi,
                    "{\"type\":\"auth\",\"ok\":true,\"message\":\"login_success\"}") < 0)
                return -1;
            lws_callback_on_writable(wsi);
            return 0;
        }

        pthread_mutex_lock(&g_json_mutex);
        if (send_ws_text(wsi, g_latest_json) < 0) {
            pthread_mutex_unlock(&g_json_mutex);
            return -1;
        }
        pthread_mutex_unlock(&g_json_mutex);
        break;

    default:
        break;
    }
    return 0;
}

/* 
 * Thread 2: Monitoreo + publicación WS
 *  */

typedef struct {
    struct lws_context *ctx;
} monitor_args_t;

static void *thread_monitor(void *arg)
{
    monitor_args_t *args = (monitor_args_t *)arg;
    struct timespec last = {0};

    printf("[THREAD-MONITOR] Iniciado. Publicando en ws://0.0.0.0:%d\n", WS_PORT);

    while (g_running) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        /* Atender sockets WS */
        lws_service(args->ctx, 100);

        /* Cada 2 segundos publicar JSON actualizado */
        if (last.tv_sec == 0 || (now.tv_sec - last.tv_sec) >= 2) {
            last = now;
            build_and_publish_json(args->ctx);
        }
    }

    printf("[THREAD-MONITOR] Terminado.\n");
    return NULL;
}

/* 
 * main
 *  */

static void on_signal(int sig) { (void)sig; g_running = 0; }

int main(void)
{
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    /* ---- PASO 1: Verificar que /proc/file_analyzer existe ---- */
    if (access(PROC_PATH, R_OK) != 0) {
        fprintf(stderr, "[ERROR] %s no encontrado. "
                        "Carga el módulo: sudo insmod file_analyzer_module.ko\n",
                PROC_PATH);
        return 1;
    }

    /* ---- PASO 2: Crear directorio de monitoreo si no existe ---- */
    init_paths();
    if (mkdir_p(g_monitor_dir) != 0) {
        fprintf(stderr, "[ERROR] No se pudo crear el directorio de monitoreo: %s\n", g_monitor_dir);
        return 1;
    }

    if (mkdir_p(HASH_DB_DIR) != 0) {
        fprintf(stderr, "[ERROR] No se pudo crear la carpeta de bd: %s\n", HASH_DB_DIR);
        return 1;
    }

    load_known_files_db();
    save_known_files_db();

    /* ---- PASO 4: Iniciar servidor WebSocket ---- */
    static struct lws_protocols protocols[] = {
        {"file-analyzer", callback_file_analyzer, sizeof(session_data_t), JSON_BUF, 0, NULL, 0},
        {NULL, NULL, 0, 0, 0, NULL, 0}
    };

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port      = WS_PORT;
    info.protocols = protocols;
    info.gid = info.uid = -1;

    g_proto = &protocols[0];
    struct lws_context *ctx = lws_create_context(&info);
    if (!ctx) {
        fprintf(stderr, "[WS] No se pudo crear contexto\n");
        return 1;
    }

    printf("[WS] Servidor en ws://0.0.0.0:%d\n", WS_PORT);
    printf("[SCAN] Directorio monitoreado: %s\n", g_monitor_dir);
    printf("[DB] Hashes en: %s\n", g_hash_db_path);
    printf("[AUTH] Login inicial desde el front habilitado\n");

    /* ---- PASO 5: Lanzar los 3 hilos ---- */
    pthread_t t_mem, t_monitor, t_scan;
    monitor_args_t margs = { ctx };

    pthread_create(&t_mem,     NULL, thread_mem_monitor, NULL);
    pthread_create(&t_monitor, NULL, thread_monitor, &margs);
    pthread_create(&t_scan,    NULL, thread_file_scan, NULL);

    /* Esperar a que terminen (por señal SIGINT/SIGTERM) */
    pthread_join(t_mem,     NULL);
    pthread_join(t_monitor, NULL);
    pthread_join(t_scan,    NULL);

    lws_context_destroy(ctx);
    printf("Daemon terminado.\n");
    return 0;
}
