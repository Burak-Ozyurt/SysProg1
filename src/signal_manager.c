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