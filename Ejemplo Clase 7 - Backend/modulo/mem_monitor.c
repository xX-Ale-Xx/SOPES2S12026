/*
 * mem_monitor.c
 *
 * Módulo de kernel que expone métricas en /proc/mem_monitor
 * con formato compacto para backend/parsers.
 */

/* Headers base de módulos y logging en kernel */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
/* Infraestructura /proc y seq_file para exponer contenido dinámico */
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
/* APIs de memoria y scheduler para obtener RSS, páginas y procesos */
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/mmzone.h>
#include <linux/vmstat.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/sort.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("mem_monitor");
MODULE_DESCRIPTION("Monitor de memoria del sistema con top 10 procesos");
MODULE_VERSION("1.0");

/* Nombre de la entrada /proc */
#define PROC_FILENAME   "mem_monitor"
/* Cantidad de procesos que se muestran en el top */
#define TOP_N           10

/* ------------------------------------------------------------------ */
/* Estructura para guardar info de un proceso                          */
/* ------------------------------------------------------------------ */
struct proc_mem_info {
    /* PID del proceso */
    pid_t  pid;
    /* Nombre corto del proceso (task->comm) */
    char   name[TASK_COMM_LEN];
    /* Resident Set Size en KB */
    unsigned long rss_kb;   /* RSS en KB */
};

/* ------------------------------------------------------------------ */
/* Comparador descendente para sort()                                   */
/* ------------------------------------------------------------------ */
static int cmp_desc(const void *a, const void *b)
{
    const struct proc_mem_info *pa = a;
    const struct proc_mem_info *pb = b;

    /* sort() espera: >0 si "a" va después, <0 si "a" va antes */
    if (pb->rss_kb > pa->rss_kb) return  1;
    if (pb->rss_kb < pa->rss_kb) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Recopila RSS de todos los procesos y devuelve array ordenado        */
/* Retorna número de entradas válidas; el caller debe kfree(arr)       */
/* ------------------------------------------------------------------ */
static int collect_top(struct proc_mem_info **out, int maxn)
{
    struct task_struct  *task;
    struct proc_mem_info *arr;
    int n = 0, capacity = 256;

    /* Reserva inicial; se amplía si hay más procesos */
    arr = kmalloc_array(capacity, sizeof(*arr), GFP_KERNEL);
    if (!arr)
        return -ENOMEM;

    /*
     * Recorremos la lista global de procesos bajo lock RCU.
     * for_each_process() es seguro con rcu_read_lock().
     */
    rcu_read_lock();
    for_each_process(task) {
        struct mm_struct *mm;
        unsigned long rss = 0;

        /* Saltar procesos de kernel (sin mm) */
        mm = get_task_mm(task);
        if (!mm)
            continue;

        /* get_mm_rss() retorna páginas; convertimos a KB */
        rss = get_mm_rss(mm) << (PAGE_SHIFT - 10); /* páginas -> KB */
        mmput(mm);

        if (n >= capacity) {
            /* Ampliar buffer dinámicamente */
            struct proc_mem_info *tmp;
            capacity *= 2;
            /* GFP_ATOMIC evita dormir dentro de sección crítica */
            tmp = krealloc(arr, capacity * sizeof(*arr), GFP_ATOMIC);
            if (!tmp) {
                /* Sin memoria: devolver lo que tenemos */
                break;
            }
            arr = tmp;
        }

        /* Copia de snapshot del proceso actual */
        arr[n].pid    = task->pid;
        arr[n].rss_kb = rss;
        get_task_comm(arr[n].name, task);
        n++;
    }
    rcu_read_unlock();

    /* Orden descendente por consumo de memoria */
    sort(arr, n, sizeof(*arr), cmp_desc, NULL);

    *out = arr;
    return (n < maxn) ? n : maxn;
}

/* ------------------------------------------------------------------ */
/* Escritura compacta en /proc/mem_monitor para backend                */
/* ------------------------------------------------------------------ */
static int mem_monitor_show(struct seq_file *m, void *v)
{
    struct sysinfo si;
    struct proc_mem_info *procs = NULL;
    unsigned long total_kb, used_kb, free_kb, cached_kb;
    unsigned long swap_used_kb;
    unsigned long active_kb, inactive_kb;
    unsigned long minor_faults = 0, major_faults = 0;
    struct task_struct *task;
    int i, n;

    /* si_meminfo() llena estructura con snapshot global de RAM/SWAP */
    si_meminfo(&si);

    /* mem_unit ajusta la unidad real reportada por el kernel */
    total_kb    = si.totalram  * (si.mem_unit >> 10);  /* bytes -> KB */
    free_kb     = si.freeram   * (si.mem_unit >> 10);
    cached_kb   = (si.bufferram + global_node_page_state(NR_FILE_PAGES)
                   - total_swapcache_pages()) * (PAGE_SIZE >> 10);
    used_kb     = total_kb - free_kb - cached_kb;

    swap_used_kb  = (si.totalswap - si.freeswap) * (si.mem_unit >> 10);

    active_kb   = global_node_page_state(NR_ACTIVE_ANON)
                + global_node_page_state(NR_ACTIVE_FILE);
    active_kb  *= (PAGE_SIZE >> 10);

    inactive_kb = global_node_page_state(NR_INACTIVE_ANON)
                + global_node_page_state(NR_INACTIVE_FILE);
    inactive_kb *= (PAGE_SIZE >> 10);

    rcu_read_lock();
    for_each_process(task) {
        minor_faults += task->min_flt;
        major_faults += task->maj_flt;
    }
    rcu_read_unlock();

    /* Formato estable para máquina */
    n = collect_top(&procs, TOP_N);
    seq_puts(m, "version=1\n");
    seq_printf(m, "total_kb=%lu\n", total_kb);
    seq_printf(m, "used_kb=%lu\n", used_kb);
    seq_printf(m, "free_kb=%lu\n", free_kb);
    seq_printf(m, "cached_kb=%lu\n", cached_kb);
    seq_printf(m, "swap_used_kb=%lu\n", swap_used_kb);
    seq_printf(m, "active_kb=%lu\n", active_kb);
    seq_printf(m, "inactive_kb=%lu\n", inactive_kb);
    seq_printf(m, "minor_faults=%lu\n", minor_faults);
    seq_printf(m, "major_faults=%lu\n", major_faults);

    if (n < 0) {
        seq_puts(m, "top_count=0\n");
        kfree(procs);
        return 0;
    }

    seq_printf(m, "top_count=%d\n", n);
    for (i = 0; i < n; i++) {
        unsigned long pct_x100 = 0;
        if (total_kb > 0)
            pct_x100 = (procs[i].rss_kb * 10000UL) / total_kb;

        /*
         * proc=pid|name|rss_kb|pct_x100
         * pct_x100: porcentaje con dos decimales implícitos.
         */
        seq_printf(m, "proc=%d|%s|%lu|%lu\n",
                   procs[i].pid,
                   procs[i].name,
                   procs[i].rss_kb,
                   pct_x100);
    }

    kfree(procs);
    return 0;
}

/* ------------------------------------------------------------------ */
/* open() para seq_file de una sola pasada                             */
/* ------------------------------------------------------------------ */
static int mem_monitor_open(struct inode *inode, struct file *file)
{
    /* single_open: genera contenido en una sola pasada al hacer read() */
    return single_open(file, mem_monitor_show, NULL);
}

/* ------------------------------------------------------------------ */
/* Operaciones de archivo para /proc                                   */
/* ------------------------------------------------------------------ */
static const struct proc_ops mem_monitor_fops = {
    /* seq_file helpers para open/read/lseek/release */
    .proc_open    = mem_monitor_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* ------------------------------------------------------------------ */
/* Inicialización del módulo                                           */
/* ------------------------------------------------------------------ */
static int __init mem_monitor_init(void)
{
    struct proc_dir_entry *entry;

    /* Única entrada para backend/parsers */
    entry = proc_create(PROC_FILENAME, 0444, NULL, &mem_monitor_fops);
    if (!entry) {
        pr_err("mem_monitor: no se pudo crear /proc/%s\n", PROC_FILENAME);
        return -ENOMEM;
    }

    pr_info("mem_monitor: modulo cargado -> /proc/%s\n", PROC_FILENAME);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Limpieza del módulo                                                  */
/* ------------------------------------------------------------------ */
static void __exit mem_monitor_exit(void)
{
    /* Remover entrada /proc */
    remove_proc_entry(PROC_FILENAME, NULL);
    pr_info("mem_monitor: modulo descargado\n");
}

module_init(mem_monitor_init);
module_exit(mem_monitor_exit);