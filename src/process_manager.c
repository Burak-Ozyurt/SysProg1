#include "process_manager.h"
#include "logger.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
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

static WorkerProcess *find_worker_by_pid(pid_t pid)
{
    if (pid <= 0) {
        return NULL;
    }
    for (size_t i = 0; i < worker_count; ++i) {
        if (workers[i].pid == pid) {
            return &workers[i];
        }
    }
    return NULL;
}

static int send_signal_to_worker(const WorkerProcess *w, int sig, const char *sig_name)
{
    if (kill(w->pid, sig) == 0) {
        log_event("SIGNAL %s %s pid=%d", sig_name, w->name, (int)w->pid);
        return 1;
    }
    if (errno == ESRCH) {
        return 0;
    }
    log_event("ERROR %s gonderilemedi %s pid=%d: %s",
              sig_name, w->name, (int)w->pid, strerror(errno));
    return -1;
}

static void record_worker_exit(WorkerProcess *w, int status)
{
    pid_t exited_pid = w->pid;
    w->pid = -1;
    w->last_exit_at = time(NULL);

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        w->last_exit_code = code;
        w->last_term_signal = 0;

        if (code == 0) {
            w->last_exit_reason = WORKER_EXIT_NORMAL;
            w->state = shutting_down ? WORKER_STATE_TERMINATED : WORKER_STATE_COMPLETED;
            log_event("EXIT %s pid=%d reason=NORMAL code=0", w->name, (int)exited_pid);
        } else {
            w->last_exit_reason = WORKER_EXIT_ERROR;
            w->state = shutting_down ? WORKER_STATE_TERMINATED : WORKER_STATE_FAILED;
            log_event("EXIT %s pid=%d reason=ERROR code=%d",
                      w->name, (int)exited_pid, code);
        }
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        w->last_exit_reason = WORKER_EXIT_SIGNAL;
        w->last_exit_code = -1;
        w->last_term_signal = sig;
        w->state = shutting_down ? WORKER_STATE_TERMINATED : WORKER_STATE_FAILED;
        log_event("EXIT %s pid=%d reason=SIGNAL sig=%d",
                  w->name, (int)exited_pid, sig);
    } else {
        w->last_exit_reason = WORKER_EXIT_NONE;
        log_event("WARN %s pid=%d beklenmedik waitpid durumu=0x%x",
                  w->name, (int)exited_pid, (unsigned int)status);
    }
}

static void maybe_restart_worker(WorkerProcess *w)
{
    if (shutting_down) {
        return;
    }

    if (w->last_exit_reason != WORKER_EXIT_ERROR &&
        w->last_exit_reason != WORKER_EXIT_SIGNAL) {
        return;
    }

    if (w->restart_count >= PM_MAX_RESTARTS) {
        w->state = WORKER_STATE_FAILED;
        log_event("GIVE_UP %s yeniden baslatma siniri asildi (%d/%d)",
                  w->name, w->restart_count, PM_MAX_RESTARTS);
        return;
    }

    w->restart_count++;
    log_event("RESTART %s (%d/%d)", w->name, w->restart_count, PM_MAX_RESTARTS);

    if (pm_start_worker(w->id) == -1) {
        w->state = WORKER_STATE_FAILED;
        log_event("ERROR %s yeniden baslatilamadi (%d/%d)",
                  w->name, w->restart_count, PM_MAX_RESTARTS);
    }
}

int pm_init(void)
{
    if (initialized) {
        pm_cleanup();
    }
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
    if (w->argc <= 0 || w->argc > PM_MAX_ARGS) {
        log_event("ERROR gecersiz arguman sayisi (%s): argc=%d",
                  w->name, w->argc);
        return -1;
    }
    if (access(w->exec_path, X_OK) == -1) {
        log_event("ERROR %s calistirilamiyor (%s): %s",
                  w->name, w->exec_path, strerror(errno));
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
        sigset_t empty_mask;
        if (sigemptyset(&empty_mask) == -1 ||
            sigprocmask(SIG_SETMASK, &empty_mask, NULL) == -1) {
            const char msg[] = "worker: sigprocmask basarisiz\n";
            if (write(STDERR_FILENO, msg, sizeof(msg) - 1) == -1) {
                /* cikis oncesi en iyi caba mesaji */
            }
            _exit(127);
        }
        execv(w->exec_path, exec_argv);
        const char msg[] = "worker: execv basarisiz\n";
        if (write(STDERR_FILENO, msg, sizeof(msg) - 1) == -1) {
            /* cikis oncesi en iyi caba mesaji */
        }
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
                if (pm_active_count() > 0) {
                    pm_force_kill_remaining();
                }
                return -1;
            }
        }
    }
    return 0;
}

int pm_reap_children(void)
{
    if (!initialized) {
        return -1;
    }

    int reaped = 0;

    for (;;) {
        int status = 0;
        pid_t pid = waitpid(-1, &status, WNOHANG);

        if (pid == 0) {
            break;
        }
        if (pid < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == ECHILD) {
                break;
            }
            log_event("ERROR waitpid basarisiz: %s", strerror(errno));
            return -1;
        }

        reaped++;
        WorkerProcess *w = find_worker_by_pid(pid);
        if (w == NULL) {
            log_event("WARN bilinmeyen alt surec toplandi pid=%d", (int)pid);
            continue;
        }

        record_worker_exit(w, status);
        maybe_restart_worker(w);
    }

    return reaped;
}

void pm_set_shutting_down(bool flag)
{
    shutting_down = flag;
}

bool pm_is_shutting_down(void)
{
    return shutting_down;
}

int pm_terminate_all(void)
{
    if (!initialized) {
        return -1;
    }

    shutting_down = true;

    int sent = 0;
    bool had_error = false;

    for (size_t i = 0; i < worker_count; ++i) {
        if (workers[i].state != WORKER_STATE_RUNNING || workers[i].pid <= 0) {
            continue;
        }
        int rc = send_signal_to_worker(&workers[i], SIGTERM, "SIGTERM");
        if (rc > 0) {
            sent++;
        } else if (rc < 0) {
            had_error = true;
        }
    }

    return had_error ? -1 : sent;
}

int pm_force_kill_remaining(void)
{
    if (!initialized) {
        return -1;
    }

    shutting_down = true;

    int killed = 0;
    bool had_error = false;

    if (pm_reap_children() == -1) {
        had_error = true;
    }

    for (size_t i = 0; i < worker_count; ++i) {
        WorkerProcess *w = &workers[i];
        if (w->state != WORKER_STATE_RUNNING || w->pid <= 0) {
            continue;
        }

        int rc = send_signal_to_worker(w, SIGKILL, "SIGKILL");
        if (rc < 0) {
            had_error = true;
            continue;
        }
        if (rc == 0) {
            continue;
        }

        killed++;

        int status = 0;
        pid_t waited;
        do {
            waited = waitpid(w->pid, &status, 0);
        } while (waited == -1 && errno == EINTR);

        if (waited == w->pid) {
            record_worker_exit(w, status);
        } else if (waited == -1 && errno == ECHILD) {
            w->pid = -1;
            w->state = WORKER_STATE_TERMINATED;
            w->last_exit_at = time(NULL);
        } else {
            log_event("ERROR SIGKILL sonrasi waitpid basarisiz %s pid=%d: %s",
                      w->name, (int)w->pid, strerror(errno));
            had_error = true;
        }
    }

    if (pm_reap_children() == -1) {
        had_error = true;
    }

    return had_error ? -1 : killed;
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

int pm_start_periodic_timer(unsigned int interval_sec)
{
    if (!initialized) {
        return -1;
    }

    unsigned int interval =
        (interval_sec == 0) ? PM_DEFAULT_CHECK_INTERVAL_SEC : interval_sec;

    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    timer.it_interval.tv_sec = (time_t)interval;
    timer.it_value.tv_sec = (time_t)interval;

    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
        log_event("ERROR setitimer baslatilamadi: %s", strerror(errno));
        return -1;
    }

    log_event("TIMER baslatildi aralik=%u sn", interval);
    return 0;
}

void pm_stop_periodic_timer(void)
{
    struct itimerval zero_timer;
    memset(&zero_timer, 0, sizeof(zero_timer));

    if (setitimer(ITIMER_REAL, &zero_timer, NULL) == -1) {
        log_event("ERROR setitimer durdurulamadi: %s", strerror(errno));
    }
}

int pm_periodic_check(void)
{
    if (!initialized) {
        return -1;
    }

    if (pm_reap_children() == -1) {
        return -1;
    }

    int alive = 0;
    for (size_t i = 0; i < worker_count; ++i) {
        WorkerProcess *w = &workers[i];
        if (w->state != WORKER_STATE_RUNNING || w->pid <= 0) {
            continue;
        }

        if (kill(w->pid, 0) == 0) {
            alive++;
        } else if (errno == ESRCH) {
            log_event("WARN periyodik kontrol: %s pid=%d bulunamadi",
                      w->name, (int)w->pid);
            w->pid = -1;
            w->state = shutting_down ? WORKER_STATE_TERMINATED : WORKER_STATE_FAILED;
            w->last_exit_at = time(NULL);
        } else {
            log_event("ERROR periyodik kontrol kill(0) %s pid=%d: %s",
                      w->name, (int)w->pid, strerror(errno));
            return -1;
        }
    }

    log_event("PERIODIC_CHECK aktif=%d toplam=%zu", alive, worker_count);
    return alive;
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
    pm_stop_periodic_timer();

    if (initialized) {
        if (pm_active_count() > 0) {
            pm_force_kill_remaining();
        }
        for (;;) {
            pid_t pid = waitpid(-1, NULL, WNOHANG);
            if (pid > 0 || (pid == -1 && errno == EINTR)) {
                continue;
            }
            break;
        }
    }

    for (size_t i = 0; i < PM_MAX_WORKERS; ++i) {
        reset_worker_slot(&workers[i], (int)i);
    }
    worker_count = 0;
    shutting_down = false;
    initialized = false;
}
