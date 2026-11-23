#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "logger.h"
#include "process_manager.h"
#include "signal_manager.h"

/* Baslatma veya ana dongu hatasinda geride worker birakmaz. */
static void cleanup_after_error(void)
{
    size_t count = 0;
    const WorkerProcess *workers = pm_get_workers(&count);

    pm_set_shutting_down(true);

    for (size_t i = 0; i < count; ++i) {
        if (workers[i].state == WORKER_STATE_RUNNING &&
            workers[i].pid > 0) {
            if (kill(workers[i].pid, SIGKILL) == -1 &&
                errno != ESRCH) {
                perror("Worker temizlenemedi");
            }
        }
    }

    for (;;) {
        pid_t pid = waitpid(-1, NULL, 0);

        if (pid > 0) {
            continue;
        }

        if (pid == -1 && errno == EINTR) {
            continue;
        }

        if (pid == -1 && errno != ECHILD) {
            perror("waitpid");
        }

        break;
    }
}

/* Kalan sure varsa 1, sure dolduysa 0, hatada -1 dondurur. */
static int shutdown_time_remaining(
    const struct timespec *deadline,
    struct timespec *remaining)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        return -1;
    }

    if (now.tv_sec > deadline->tv_sec ||
        (now.tv_sec == deadline->tv_sec &&
         now.tv_nsec >= deadline->tv_nsec)) {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
        return 0;
    }

    remaining->tv_sec = deadline->tv_sec - now.tv_sec;
    remaining->tv_nsec = deadline->tv_nsec - now.tv_nsec;

    if (remaining->tv_nsec < 0) {
        remaining->tv_sec--;
        remaining->tv_nsec += 1000000000L;
    }

    return 1;
}

int main(void)
{
    int result = EXIT_FAILURE;
    sigset_t blocked_mask;
    sigset_t old_mask;
    sigset_t wait_mask;
    struct timespec shutdown_deadline = {0};

    const int signals[] = {
        SIGINT, SIGTERM, SIGUSR1, SIGCHLD, SIGALRM
    };

    if (sigemptyset(&blocked_mask) == -1) {
        perror("sigemptyset");
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
        if (sigaddset(&blocked_mask, signals[i]) == -1) {
            perror("sigaddset");
            return EXIT_FAILURE;
        }
    }

    if (sigprocmask(SIG_BLOCK, &blocked_mask, &old_mask) == -1) {
        perror("sigprocmask");
        return EXIT_FAILURE;
    }

    wait_mask = old_mask;

    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
        if (sigdelset(&wait_mask, signals[i]) == -1) {
            perror("sigdelset");
            goto restore_mask;
        }
    }

    sm_reset_events();

    if (sm_install_handlers() == -1) {
        perror("Sinyal isleyicileri kurulamadi");
        goto restore_mask;
    }

    if (log_open(NULL) == -1) {
        goto restore_mask;
    }

    if (pm_init() == -1) {
        log_event("ERROR surec yoneticisi baslatilamadi");
        goto close_log;
    }

    const char *worker1[] = {
        "./worker", "worker-1", "forever", NULL
    };

    const char *worker2[] = {
        "./worker", "worker-2", "normal", "3", NULL
    };

    const char *worker3[] = {
        "./worker", "worker-3", "crash", "2", NULL
    };

    const char *worker4[] = {
        "./worker", "worker-4", "stubborn", NULL
    };

    if (pm_add_worker("worker-1", "./worker", worker1) == -1 ||
        pm_add_worker("worker-2", "./worker", worker2) == -1 ||
        pm_add_worker("worker-3", "./worker", worker3) == -1 ||
        pm_add_worker("worker-4", "./worker", worker4) == -1) {
        log_event("ERROR worker tanimlari eklenemedi");
        goto cleanup;
    }

    if (pm_start_all() == -1) {
        log_event("ERROR worker baslatma basarisiz");
        goto cleanup;
    }

    if (pm_start_periodic_timer(0) == -1) {
        log_event("ERROR periyodik zamanlayici baslatilamadi");
        goto cleanup;
    }

    log_event("WATCHDOG START pid=%d", (int)getpid());
    printf("Watchdog PID: %d\n", (int)getpid());
    fflush(stdout);

    while (pm_active_count() > 0) {
        if (sm_shutdown_requested) {
            sm_shutdown_requested = 0;

            if (!pm_is_shutting_down()) {
                pm_set_shutting_down(true);
                pm_stop_periodic_timer();

                if (clock_gettime(
                        CLOCK_MONOTONIC, &shutdown_deadline) == -1) {
                    perror("clock_gettime");
                    goto cleanup;
                }

                shutdown_deadline.tv_sec += PM_SHUTDOWN_TIMEOUT_SEC;

                log_event("SHUTDOWN basladi: zaman asimi=%d saniye",
                          PM_SHUTDOWN_TIMEOUT_SEC);

                if (pm_terminate_all() == -1) {
                    log_event("ERROR worker'lara SIGTERM gonderilemedi");
                    goto cleanup;
                }
            }
        }

        if (sm_child_exited) {
            sm_child_exited = 0;

            if (pm_reap_children() == -1) {
                goto cleanup;
            }
        }

        if (sm_status_requested) {
            sm_status_requested = 0;
            log_status_report();
        }

        if (sm_timer_expired) {
            sm_timer_expired = 0;

            if (!pm_is_shutting_down()) {
                if (pm_periodic_check() == -1) {
                    log_event("ERROR periyodik kontrol basarisiz");
                    goto cleanup;
                }
            }
        }

        if (pm_active_count() == 0) {
            break;
        }

        if (pm_is_shutting_down()) {
            struct timespec remaining;
            int time_left = shutdown_time_remaining(
                &shutdown_deadline, &remaining);

            if (time_left == -1) {
                perror("clock_gettime");
                goto cleanup;
            }

            if (time_left == 0) {
                if (pm_reap_children() == -1) {
                    goto cleanup;
                }

                if (pm_active_count() > 0) {
                    log_event(
                        "SHUTDOWN TIMEOUT: kalan %d worker'a SIGKILL",
                        pm_active_count());

                    if (pm_force_kill_remaining() == -1) {
                        log_event("ERROR zorla sonlandirma basarisiz");
                        goto cleanup;
                    }

                    if (pm_active_count() > 0) {
                        log_event("ERROR toplanamayan worker var");
                        goto cleanup;
                    }
                }

                break;
            }

            if (pselect(0, NULL, NULL, NULL,
                        &remaining, &wait_mask) == -1 &&
                errno != EINTR) {
                perror("pselect");
                goto cleanup;
            }
        } else {
            if (sigsuspend(&wait_mask) == -1 && errno != EINTR) {
                perror("sigsuspend");
                goto cleanup;
            }
        }
    }

    if (pm_is_shutting_down()) {
        log_event("SHUTDOWN tamamlandi: tum worker'lar toplandi");
    } else {
        log_event("WATCHDOG END tum worker'lar toplandi");
    }

    result = EXIT_SUCCESS;

cleanup:
    pm_stop_periodic_timer();

    if (result != EXIT_SUCCESS) {
        cleanup_after_error();
    }

    pm_cleanup();

close_log:
    log_close();

restore_mask:
    if (sigprocmask(SIG_SETMASK, &old_mask, NULL) == -1) {
        perror("Sinyal maskesi geri yuklenemedi");
        result = EXIT_FAILURE;
    }

    return result;
}