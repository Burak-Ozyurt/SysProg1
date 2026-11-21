#include "process_manager.h"
#include "logger.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static WorkerProcess workers[PM_MAX_WORKERS];
static size_t worker_count = 0;
static bool shutting_down = false;
static bool initialized = false;

static int copy_string(char *dst, size_t dst_size, const char *src)
{
    if (dst == NULL || src == NULL || dst_size == 0) {
        return -1;
    }
    int written = snprintf(dst, dst_size, "%s", src);
    if (written < 0 || (size_t)written >= dst_size) {
        return -1;
    }
    return 0;
}

static void reset_worker_slot(WorkerProcess *w, int id)
{
    memset(w, 0, sizeof(*w));
    w->id = id;
    w->pid = -1;
    w->state = WORKER_STATE_NOT_STARTED;
    w->last_exit_reason = WORKER_EXIT_NONE;
    w->last_exit_code = -1;
    w->last_term_signal = 0;
    w->restart_count = 0;
}

int pm_init(void)
{
    for (size_t i = 0; i < PM_MAX_WORKERS; ++i) {
        reset_worker_slot(&workers[i], (int)i);
    }
    worker_count = 0;
    shutting_down = false;
    initialized = true;
    return 0;
}

int pm_add_worker(const char *name, const char *exec_path, const char *const argv[])
{
    if (!initialized || name == NULL || name[0] == '\0' ||
        exec_path == NULL || exec_path[0] == '\0') {
        return -1;
    }
    if (worker_count >= PM_MAX_WORKERS) {
        log_event("ERROR pm_add_worker: maksimum worker siniri (%d) asildi",
                  PM_MAX_WORKERS);
        return -1;
    }

    WorkerProcess *w = &workers[worker_count];
    reset_worker_slot(w, (int)worker_count);

    if (copy_string(w->name, sizeof(w->name), name) == -1 ||
        copy_string(w->exec_path, sizeof(w->exec_path), exec_path) == -1) {
        reset_worker_slot(w, (int)worker_count);
        return -1;
    }

    if (argv == NULL || argv[0] == NULL) {
        if (copy_string(w->args[0], sizeof(w->args[0]), exec_path) == -1) {
            reset_worker_slot(w, (int)worker_count);
            return -1;
        }
        w->argc = 1;
    } else {
        int arg_idx = 0;
        while (argv[arg_idx] != NULL) {
            if (arg_idx >= PM_MAX_ARGS) {
                reset_worker_slot(w, (int)worker_count);
                return -1;
            }
            if (copy_string(w->args[arg_idx], sizeof(w->args[arg_idx]), argv[arg_idx]) == -1) {
                reset_worker_slot(w, (int)worker_count);
                return -1;
            }
            arg_idx++;
        }
        w->argc = arg_idx;
    }

    worker_count++;
    return w->id;
}

int pm_start_worker(int worker_id)
{
    if (!initialized || worker_id < 0 || (size_t)worker_id >= worker_count) {
        return -1;
    }

    WorkerProcess *w = &workers[worker_id];

    if (shutting_down) {
        log_event("WARN %s kapanis sirasinda baslatilamadi", w->name);
        return -1;
    }
    if (w->state == WORKER_STATE_RUNNING && w->pid > 0) {
        return -1;
    }

    char *exec_argv[PM_MAX_ARGS + 1];
    for (int i = 0; i < w->argc && i < PM_MAX_ARGS; ++i) {
        exec_argv[i] = w->args[i];
    }
    exec_argv[w->argc] = NULL;

    pid_t pid = fork();
    if (pid < 0) {
        log_event("ERROR fork basarisiz (%s): %s", w->name, strerror(errno));
        return -1;
    }

    if (pid == 0) {
        execv(w->exec_path, exec_argv);
        perror("execv");
        _exit(127);
    }

    w->pid = pid;
    w->state = WORKER_STATE_RUNNING;
    w->started_at = time(NULL);

    log_event("START %s pid=%d", w->name, (int)w->pid);
    return 0;
}

int pm_start_all(void)
{
    if (!initialized || worker_count == 0 || shutting_down) {
        return -1;
    }

    for (size_t i = 0; i < worker_count; ++i) {
        if (workers[i].state == WORKER_STATE_NOT_STARTED) {
            if (pm_start_worker((int)i) == -1) {
                return -1;
            }
        }
    }
    return 0;
}

void pm_set_shutting_down(bool flag)
{
    shutting_down = flag;
}

bool pm_is_shutting_down(void)
{
    return shutting_down;
}

int pm_active_count(void)
{
    if (!initialized) {
        return 0;
    }

    int active = 0;
    for (size_t i = 0; i < worker_count; ++i) {
        if (workers[i].state == WORKER_STATE_RUNNING && workers[i].pid > 0) {
            active++;
        }
    }
    return active;
}

const WorkerProcess *pm_get_workers(size_t *count)
{
    if (count != NULL) {
        *count = initialized ? worker_count : 0;
    }
    return workers;
}

const char *pm_state_to_string(WorkerState state)
{
    switch (state) {
    case WORKER_STATE_NOT_STARTED:
        return "NOT_STARTED";
    case WORKER_STATE_RUNNING:
        return "RUNNING";
    case WORKER_STATE_COMPLETED:
        return "COMPLETED";
    case WORKER_STATE_FAILED:
        return "FAILED";
    case WORKER_STATE_TERMINATED:
        return "TERMINATED";
    default:
        return "UNKNOWN";
    }
}

const char *pm_exit_reason_to_string(WorkerExitReason reason)
{
    switch (reason) {
    case WORKER_EXIT_NONE:
        return "NONE";
    case WORKER_EXIT_NORMAL:
        return "NORMAL";
    case WORKER_EXIT_ERROR:
        return "ERROR";
    case WORKER_EXIT_SIGNAL:
        return "SIGNAL";
    default:
        return "UNKNOWN";
    }
}

void pm_cleanup(void)
{
    for (size_t i = 0; i < PM_MAX_WORKERS; ++i) {
        reset_worker_slot(&workers[i], (int)i);
    }
    worker_count = 0;
    shutting_down = false;
    initialized = false;
}
