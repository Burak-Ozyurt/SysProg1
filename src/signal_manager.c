#include <stddef.h>

#include "signal_manager.h"

volatile sig_atomic_t sm_shutdown_requested = 0;
volatile sig_atomic_t sm_status_requested = 0;
volatile sig_atomic_t sm_child_exited = 0;
volatile sig_atomic_t sm_timer_expired = 0;

void sm_reset_events(void)
{
    sm_shutdown_requested = 0;
    sm_status_requested = 0;
    sm_child_exited = 0;
    sm_timer_expired = 0;
}

static void sm_handle_signal(int signo)
{
    switch (signo) {
    case SIGINT:
    case SIGTERM:
        sm_shutdown_requested = 1;
        break;
    case SIGUSR1:
        sm_status_requested = 1;
        break;
    case SIGCHLD:
        sm_child_exited = 1;
        break;
    case SIGALRM:
        sm_timer_expired = 1;
        break;
    }
}

int sm_install_handlers(void)
{
    const int signals[] = {
        SIGINT, SIGTERM, SIGUSR1, SIGCHLD, SIGALRM
    };

    struct sigaction action = {0};
    action.sa_handler = sm_handle_signal;

    if (sigemptyset(&action.sa_mask) == -1) {
        return -1;
    }

    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
        action.sa_flags = SA_RESTART;

        if (signals[i] == SIGCHLD) {
            action.sa_flags |= SA_NOCLDSTOP;
        }

        if (sigaction(signals[i], &action, NULL) == -1) {
            return -1;
        }
    }

    return 0;
}