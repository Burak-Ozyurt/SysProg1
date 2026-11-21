#include <stdio.h>
#include <stdlib.h>

#include "signal_manager.h"

int main(void)
{
    sm_reset_events();

    if (sm_install_handlers() == -1) {
        perror("Sinyal isleyicileri kurulamadi");
        return EXIT_FAILURE;
    }

    puts("Watchdog sinyal isleyicileri kuruldu.");

    return EXIT_SUCCESS;
}