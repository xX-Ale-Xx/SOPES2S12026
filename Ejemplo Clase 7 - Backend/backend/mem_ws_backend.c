/*
 * Habilita APIs POSIX modernas (gmtime_r, clock_gettime, etc.).
 * Debe definirse antes de los includes.
 */
#define _POSIX_C_SOURCE 200809L

#include <libwebsockets.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Puerto WS donde escuchará el backend */
#define WS_PORT 8080
/* Buffer máximo para el JSON serializado que se enviará por WS */
#define JSON_BUF 65536
/* Archivo /proc expuesto por el módulo del kernel */
#define PROC_PATH "/proc/mem_monitor"

/* Estructura de un proceso en el top de consumo de memoria */
typedef struct {
    int pid;
    char name[64];
    unsigned long rss_kb;
    unsigned long pct_x100;
} proc_item_t;

/* Snapshot completo de métricas que se publica por WS */
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
    proc_item_t top[16];
} monitor_data_t;

static volatile sig_atomic_t g_running = 1;
/* Último JSON generado; se reutiliza para enviarlo a todos los clientes */
static char g_latest_json[JSON_BUF] = "{\"error\":\"waiting_data\"}";

/* Puntero al protocolo para marcar todos los clientes como escribibles */
static const struct lws_protocols *g_protocol = NULL;

/* Handler de señales para terminar el loop principal limpiamente */
static void on_sigint(int sig)
{
    (void)sig;
    g_running = 0;
}

/*
 * Escapa caracteres sensibles para JSON en nombres de proceso.
 * Evita romper el JSON si aparece comilla o backslash.
 */
static size_t json_escape(char *dst, size_t dst_sz, const char *src)
{
    size_t j = 0;
    size_t i;

    for (i = 0; src[i] != '\0' && j + 1 < dst_sz; i++) {
        char c = src[i];
        if ((c == '"' || c == '\\') && j + 2 < dst_sz) {
            dst[j++] = '\\';
            dst[j++] = c;
            continue;
        }
        if ((unsigned char)c < 0x20) {
            continue;
        }
        dst[j++] = c;
    }

    dst[j] = '\0';
    return j;
}

static int read_monitor_data(monitor_data_t *data)
{
    FILE *fp;
    char line[512];
    int idx = 0;

    memset(data, 0, sizeof(*data));

    fp = fopen(PROC_PATH, "r");
    if (!fp)
        return -1;

    /*
     * Parsea formato compacto de /proc/mem_monitor:
     *   key=value
     *   proc=pid|name|rss_kb|pct_x100
     */
    while (fgets(line, sizeof(line), fp)) {
        unsigned long v = 0;
        int pid;
        char name[64];
        unsigned long rss;
        unsigned long pct_x100;

        if (sscanf(line, "total_kb=%lu", &v) == 1) {
            data->total_kb = v;
            continue;
        }
        if (sscanf(line, "used_kb=%lu", &v) == 1) {
            data->used_kb = v;
            continue;
        }
        if (sscanf(line, "free_kb=%lu", &v) == 1) {
            data->free_kb = v;
            continue;
        }
        if (sscanf(line, "cached_kb=%lu", &v) == 1) {
            data->cached_kb = v;
            continue;
        }
        if (sscanf(line, "swap_used_kb=%lu", &v) == 1) {
            data->swap_used_kb = v;
            continue;
        }
        if (sscanf(line, "active_kb=%lu", &v) == 1) {
            data->active_kb = v;
            continue;
        }
        if (sscanf(line, "inactive_kb=%lu", &v) == 1) {
            data->inactive_kb = v;
            continue;
        }
        if (sscanf(line, "minor_faults=%lu", &v) == 1) {
            data->minor_faults = v;
            continue;
        }
        if (sscanf(line, "major_faults=%lu", &v) == 1) {
            data->major_faults = v;
            continue;
        }

        if (idx < 16 &&
            sscanf(line, "proc=%d|%63[^|]|%lu|%lu", &pid, name, &rss, &pct_x100) == 4) {
            data->top[idx].pid = pid;
            snprintf(data->top[idx].name, sizeof(data->top[idx].name), "%s", name);
            data->top[idx].rss_kb = rss;
            data->top[idx].pct_x100 = pct_x100;
            idx++;
        }
    }
    fclose(fp);

    data->top_count = idx;

    /* Fallback mínimo si total_kb no llegó en la salida */
    if (data->total_kb == 0) {
        unsigned long approx = data->used_kb + data->free_kb + data->cached_kb;
        data->total_kb = approx;
    }

    return 0;
}

static int build_json(char *out, size_t out_sz, const monitor_data_t *d)
{
    size_t off = 0;
    time_t now = time(NULL);
    struct tm tm_utc;
    char ts[64];
    char esc_name[128];
    int i;

    /* Timestamp en UTC para facilitar consumo en frontend/servicios */
    gmtime_r(&now, &tm_utc);
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

    /* Abre JSON raíz y bloques de memoria/fallos de página */
    off += snprintf(out + off, out_sz - off,
                    "{\"timestamp\":\"%s\",\"memory\":{"
                    "\"total_kb\":%lu,\"used_kb\":%lu,\"free_kb\":%lu,"
                    "\"cached_kb\":%lu,\"swap_used_kb\":%lu,"
                    "\"active_kb\":%lu,\"inactive_kb\":%lu},"
                    "\"page_faults\":{\"minor\":%lu,\"major\":%lu},"
                    "\"top_processes\":[",
                    ts,
                    d->total_kb, d->used_kb, d->free_kb,
                    d->cached_kb, d->swap_used_kb,
                    d->active_kb, d->inactive_kb,
                    d->minor_faults, d->major_faults);

    if (off >= out_sz)
        return -1;

    for (i = 0; i < d->top_count; i++) {
        const proc_item_t *p = &d->top[i];
        unsigned long pct_int = p->pct_x100 / 100;
        unsigned long pct_dec = p->pct_x100 % 100;

        /* Escapar nombre para mantener JSON válido */
        json_escape(esc_name, sizeof(esc_name), p->name);

        off += snprintf(out + off, out_sz - off,
                        "%s{\"pid\":%d,\"name\":\"%s\",\"rss_kb\":%lu,"
                        "\"mem_percent\":%lu.%02lu}",
                        (i == 0) ? "" : ",",
                        p->pid, esc_name, p->rss_kb,
                        pct_int, pct_dec);

        if (off >= out_sz)
            return -1;
    }

    off += snprintf(out + off, out_sz - off, "]}");
    if (off >= out_sz)
        return -1;

    return 0;
}

static int callback_mem_monitor(struct lws *wsi,
                                enum lws_callback_reasons reason,
                                void *user,
                                void *in,
                                size_t len)
{
    unsigned char *buf;
    unsigned char *p;
    size_t payload_len;
    int n;

    (void)user;
    (void)in;
    (void)len;

    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
        /* Cliente conectado correctamente al protocolo mem-monitor */
        lwsl_user("Cliente WS conectado\n");
        break;
    case LWS_CALLBACK_SERVER_WRITEABLE:
        /*
         * libwebsockets exige reservar LWS_PRE bytes antes del payload.
         * Ahí coloca metadatos internos del frame.
         */
        payload_len = strlen(g_latest_json);
        buf = malloc(LWS_PRE + payload_len);
        if (!buf)
            return -1;
        p = &buf[LWS_PRE];
        memcpy(p, g_latest_json, payload_len);
        n = lws_write(wsi, p, payload_len, LWS_WRITE_TEXT);
        free(buf);
        if (n < 0)
            return -1;
        break;
    default:
        break;
    }

    return 0;
}

int main(void)
{
    struct lws_context_creation_info info;
    struct lws_context *context;
    monitor_data_t data;
    struct timespec last = {0};

    /* Lista de protocolos WS soportados por el servidor */
    static struct lws_protocols protocols[] = {
        {"mem-monitor", callback_mem_monitor, 0, JSON_BUF, 0, NULL, 0},
        {NULL, NULL, 0, 0, 0, NULL, 0}
    };

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    /* Configuración base del contexto WS */
    memset(&info, 0, sizeof(info));
    info.port = WS_PORT;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;

    /* Crea el servidor WS */
    g_protocol = &protocols[0];
    context = lws_create_context(&info);
    if (!context) {
        fprintf(stderr, "No se pudo crear contexto libwebsockets\n");
        return 1;
    }

    printf("WS backend (libwebsockets) en ws://0.0.0.0:%d\n", WS_PORT);
    printf("Fuente: %s\n", PROC_PATH);

    /*
     * Loop principal:
     * 1) atiende sockets WS
     * 2) cada 1s lee /proc, arma JSON y marca clientes para escritura
     */
    while (g_running) {
        struct timespec now;

        /* Espera / procesa eventos de red por 200ms */
        lws_service(context, 200);

        clock_gettime(CLOCK_MONOTONIC, &now);
        if (last.tv_sec == 0 || (now.tv_sec - last.tv_sec) >= 1) {
            last = now;

            /* Si hay datos válidos, publica snapshot a todos los clientes */
            if (read_monitor_data(&data) == 0 &&
                build_json(g_latest_json, sizeof(g_latest_json), &data) == 0) {
                lws_callback_on_writable_all_protocol(context, g_protocol);
            } else {
                /* Si falla lectura/parsing, envía error también por WS */
                snprintf(g_latest_json, sizeof(g_latest_json),
                         "{\"error\":\"cannot_read_%s\"}", PROC_PATH);
                lws_callback_on_writable_all_protocol(context, g_protocol);
            }
        }
    }

    /* Libera recursos de libwebsockets */
    lws_context_destroy(context);
    return 0;
}
