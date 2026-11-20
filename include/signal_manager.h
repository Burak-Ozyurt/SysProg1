#ifndef SIGNAL_MANAGER_H
#define SIGNAL_MANAGER_H

#include <signal.h>

/* Sinyal bildirimleri ana döngüde işlenecek. */
extern volatile sig_atomic_t sm_shutdown_requested;
extern volatile sig_atomic_t sm_status_requested;
extern volatile sig_atomic_t sm_child_exited;
extern volatile sig_atomic_t sm_timer_expired;

/* Başlangıçtaki bildirim bayraklarını sıfırlar. */
void sm_reset_events(void);

#endif  