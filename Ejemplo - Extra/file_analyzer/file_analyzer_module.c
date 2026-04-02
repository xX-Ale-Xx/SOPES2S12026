/*
 * file_analyzer_module.c
 * =====================================================================
 * Módulo de kernel que expone análisis de archivos a través de /proc.
 *
 * LO QUE HACE ESTE MÓDULO:
 *   - Calcula el SHA-256 de un archivo dado su ruta.
 *   - Retorna tamaño y timestamp de última modificación.
 *   - Detecta cambios comparando el hash actual vs el anterior.
 *   - Expone todo via /proc/file_analyzer (lectura de resultados)
 *     y /proc/file_analyzer_cmd (escritura de rutas a analizar).
 *
 * COMO SYSCALL (lo que los alumnos deben hacer):
 *   Esta misma lógica iría dentro de una función SYSCALL_DEFINE2(...)
 *   registrada en la tabla de syscalls. El módulo es solo para poder
 *   probar sin recompilar el kernel completo.
 *
 * COMPILAR:
 *   make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
 *
 * CARGAR:
 *   sudo insmod file_analyzer_module.ko
 *   sudo rmmod file_analyzer_module
 *
 * USO DESDE USERSPACE:
 *   echo "/ruta/al/archivo" | sudo tee /proc/file_analyzer_cmd
 *   cat /proc/file_analyzer
 * =====================================================================
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/time.h>
#include <crypto/hash.h>
#include <linux/scatterlist.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ejemplo SO2 - USAC");
MODULE_DESCRIPTION("Analizador de archivos: SHA-256, timestamps, deteccion de cambios");
MODULE_VERSION("1.0");

/* ---- Constantes ---- */
#define SHA256_DIGEST_SIZE   32
#define SHA256_HEX_SIZE      (SHA256_DIGEST_SIZE * 2 + 1)
#define MAX_PATH_LEN         256
#define PROC_NAME_READ       "file_analyzer"
#define PROC_NAME_CMD        "file_analyzer_cmd"

/* ---- Estado interno del módulo ---- */
static char   g_filepath[MAX_PATH_LEN]  = "";   /* Ruta actual a analizar    */
static char   g_hash_hex[SHA256_HEX_SIZE] = ""; /* Hash resultado            */
static char   g_prev_hash[SHA256_HEX_SIZE] = "";/* Hash de la lectura anterior*/
static loff_t g_filesize   = 0;                 /* Tamaño en bytes           */
static long   g_mtime_sec  = 0;                 /* Timestamp ultima modificac.*/
static int    g_changed    = 0;                 /* 1 si el hash cambio        */
static int    g_analyzed   = 0;                 /* 1 si ya hay datos validos  */
static int    g_error      = 0;                 /* 1 si hubo error            */
static char   g_errmsg[128] = "";

static DEFINE_MUTEX(g_lock); /* Protege acceso concurrente al estado global */

/*
 * sha256_of_file()
 * Calcula SHA-256 del contenido de un archivo en kernel space.
 * Retorna 0 en éxito, negativo en error.
 */
static int sha256_of_file(const char *path, u8 *digest_out)
{
    struct file         *filp;
    struct crypto_shash *tfm;
    struct shash_desc   *desc;
    u8                  *buf;
    ssize_t              bytes_read;
    loff_t               pos = 0;
    int                  ret = 0;
    size_t               desc_size;

    /* 1. Abrir el archivo en modo lectura */
    filp = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(filp))
        return PTR_ERR(filp);

    /* 2. Alocar el transform SHA-256 */
    tfm = crypto_alloc_shash("sha256", 0, 0);
    if (IS_ERR(tfm)) {
        ret = PTR_ERR(tfm);
        goto close_file;
    }

    /* 3. Alocar descriptor del hash (shash_desc + estado interno) */
    desc_size = sizeof(*desc) + crypto_shash_descsize(tfm);
    desc = kmalloc(desc_size, GFP_KERNEL);
    if (!desc) {
        ret = -ENOMEM;
        goto free_tfm;
    }
    desc->tfm = tfm;

    /* 4. Buffer temporal para leer bloques del archivo */
    buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if (!buf) {
        ret = -ENOMEM;
        goto free_desc;
    }

    /* 5. Iniciar cálculo */
    ret = crypto_shash_init(desc);
    if (ret)
        goto free_buf;

    /* 6. Alimentar el hash con el contenido del archivo en bloques */
    while ((bytes_read = kernel_read(filp, buf, PAGE_SIZE, &pos)) > 0) {
        ret = crypto_shash_update(desc, buf, bytes_read);
        if (ret)
            goto free_buf;
    }

    if (bytes_read < 0) {
        ret = bytes_read;
        goto free_buf;
    }

    /* 7. Finalizar y obtener el digest de 32 bytes */
    ret = crypto_shash_final(desc, digest_out);

free_buf:
    kfree(buf);
free_desc:
    kfree(desc);
free_tfm:
    crypto_free_shash(tfm);
close_file:
    filp_close(filp, NULL);
    return ret;
}

/* 
 * analyze_file()
 * Lógica principal: abre el archivo, obtiene metadata y hash.
 * Compara con hash anterior para detectar cambios.
 */
static void analyze_file(const char *path)
{
    struct file     *filp;
    struct kstat     st;
    u8               digest[SHA256_DIGEST_SIZE];
    char             new_hash[SHA256_HEX_SIZE];
    int              ret, i;

    mutex_lock(&g_lock);

    g_analyzed = 0;
    g_error    = 0;
    g_changed  = 0;

    /* --- Paso 1: Abrir y obtener metadata --- */
    filp = filp_open(path, O_RDONLY, 0);
    if (IS_ERR(filp)) {
        snprintf(g_errmsg, sizeof(g_errmsg),
                 "No se pudo abrir el archivo (err=%ld)", PTR_ERR(filp));
        g_error = 1;
        goto out_unlock;
    }

    /* vfs_getattr para obtener tamaño y timestamps */
    ret = vfs_getattr(&filp->f_path, &st, STATX_SIZE | STATX_MTIME,
                      AT_STATX_SYNC_AS_STAT);
    if (ret) {
        snprintf(g_errmsg, sizeof(g_errmsg),
                 "vfs_getattr fallo (err=%d)", ret);
        g_error = 1;
        filp_close(filp, NULL);
        goto out_unlock;
    }

    g_filesize  = st.size;
    g_mtime_sec = st.mtime.tv_sec;

    filp_close(filp, NULL);

    /* --- Paso 2: Calcular SHA-256 --- */
    ret = sha256_of_file(path, digest);
    if (ret) {
        snprintf(g_errmsg, sizeof(g_errmsg),
                 "sha256 fallo (err=%d)", ret);
        g_error = 1;
        goto out_unlock;
    }

    /* Convertir digest a hexadecimal legible */
    for (i = 0; i < SHA256_DIGEST_SIZE; i++)
        snprintf(new_hash + i * 2, 3, "%02x", digest[i]);
    new_hash[SHA256_HEX_SIZE - 1] = '\0';

    /* --- Paso 3: Detectar cambio vs lectura anterior --- */
    if (g_prev_hash[0] != '\0') {
        /* Ya existe un hash previo: comparar */
        g_changed = (strncmp(g_prev_hash, new_hash, SHA256_HEX_SIZE) != 0);
    }

    /* Actualizar estado global */
    strncpy(g_prev_hash, new_hash, SHA256_HEX_SIZE);
    strncpy(g_hash_hex,  new_hash, SHA256_HEX_SIZE);
    strncpy(g_filepath,  path,     MAX_PATH_LEN - 1);

    g_analyzed = 1;
    g_error    = 0;

out_unlock:
    mutex_unlock(&g_lock);
}

/* 
 * /proc/file_analyzer — lectura (seq_file)
 * Devuelve los resultados en formato clave=valor, compatible con
 * el programa intermedio en C que ya saben parsear.
 */
static int proc_show(struct seq_file *m, void *v)
{
    mutex_lock(&g_lock);

    if (g_error) {
        seq_printf(m, "status=error\n");
        seq_printf(m, "error_msg=%s\n", g_errmsg);
        mutex_unlock(&g_lock);
        return 0;
    }

    if (!g_analyzed) {
        seq_printf(m, "status=waiting\n");
        seq_printf(m, "hint=echo /ruta/archivo | sudo tee /proc/file_analyzer_cmd\n");
        mutex_unlock(&g_lock);
        return 0;
    }

    seq_printf(m, "status=ok\n");
    seq_printf(m, "path=%s\n",    g_filepath);
    seq_printf(m, "size_bytes=%lld\n", (long long)g_filesize);
    seq_printf(m, "mtime=%ld\n",  g_mtime_sec);
    seq_printf(m, "sha256=%s\n",  g_hash_hex);
    seq_printf(m, "changed=%d\n", g_changed);   /* 1=modificado, 0=igual */

    mutex_unlock(&g_lock);
    return 0;
}

static int proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, proc_show, NULL);
}

/* 
 * /proc/file_analyzer_cmd — escritura
 * El usuario escribe la ruta del archivo a analizar
 */
static ssize_t proc_cmd_write(struct file *file, const char __user *ubuf,
                               size_t count, loff_t *ppos)
{
    char kbuf[MAX_PATH_LEN];
    size_t len;

    if (count >= MAX_PATH_LEN)
        return -EINVAL;

    /* Copiar ruta desde userspace al kernel */
    if (copy_from_user(kbuf, ubuf, count))
        return -EFAULT;

    kbuf[count] = '\0';

    /* Quitar newline si lo hay */
    len = strlen(kbuf);
    if (len > 0 && kbuf[len - 1] == '\n')
        kbuf[--len] = '\0';

    if (len == 0)
        return -EINVAL;

    /* Disparar el análisis del archivo */
    analyze_file(kbuf);

    return count;
}

/* ---- Tabla de operaciones del proc cmd (solo escritura) ---- */
static const struct proc_ops cmd_proc_ops = {
    .proc_write = proc_cmd_write,
};

/* ---- Tabla de operaciones del proc de lectura ---- */
static const struct proc_ops read_proc_ops = {
    .proc_open    = proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* 
 * init / exit
 */
static int __init file_analyzer_init(void)
{
    /* Crear /proc/file_analyzer (solo lectura) */
    if (!proc_create(PROC_NAME_READ, 0444, NULL, &read_proc_ops)) {
        pr_err("file_analyzer: no se pudo crear /proc/%s\n", PROC_NAME_READ);
        return -ENOMEM;
    }

    /* Crear /proc/file_analyzer_cmd (solo escritura) */
    if (!proc_create(PROC_NAME_CMD, 0222, NULL, &cmd_proc_ops)) {
        pr_err("file_analyzer: no se pudo crear /proc/%s\n", PROC_NAME_CMD);
        remove_proc_entry(PROC_NAME_READ, NULL);
        return -ENOMEM;
    }

    pr_info("file_analyzer: modulo cargado\n");
    pr_info("  Usa:  echo '/ruta/archivo' | sudo tee /proc/file_analyzer_cmd\n");
    pr_info("  Lee:  cat /proc/file_analyzer\n");
    return 0;
}

static void __exit file_analyzer_exit(void)
{
    remove_proc_entry(PROC_NAME_CMD,  NULL);
    remove_proc_entry(PROC_NAME_READ, NULL);
    pr_info("file_analyzer: modulo descargado\n");
}

module_init(file_analyzer_init);
module_exit(file_analyzer_exit);
