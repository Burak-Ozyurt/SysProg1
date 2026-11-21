#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t got_sigterm = 0;

static void handle_sigterm_ignore(int signo)
{
    (void)signo;
    got_sigterm = 1;
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Kullanim: %s <worker_adi> [mod] [sure_sn]\n"
            "Modlar:\n"
            "  forever      Sinyal gelene kadar surekli calisir (varsayilan; steady)\n"
            "  normal       Belirtilen sure (varsayilan 3 sn) sonunda 0 ile cikar\n"
            "  error        Belirtilen sure (varsayilan 2 sn) sonunda 1 ile cikar\n"
            "  stubborn     SIGTERM'i yoksayar, SIGKILL gelene kadar calisir (ignore-term)\n"
            "  crash        Belirtilen sure (varsayilan 2 sn) sonunda SIGABRT uretir\n",
            prog);
}

static int parse_duration(const char *arg, int fallback)
{
    if (arg == NULL || arg[0] == '\0') {
        return fallback;
    }

    char *end = NULL;
    long val = strtol(arg, &end, 10);
    if (end == arg || *end != '\0' || val <= 0 || val > 3600) {
        return fallback;
    }
    return (int)val;
}

static int install_ignore_sigterm(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigterm_ignore;
    if (sigemptyset(&sa.sa_mask) == -1) {
        return -1;
    }
    sa.sa_flags = SA_RESTART;
    return sigaction(SIGTERM, &sa, NULL);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *name = argv[1];
    const char *mode = (argc >= 3) ? argv[2] : "forever";
    const char *dur_arg = (argc >= 4) ? argv[3] : NULL;
    pid_t pid = getpid();

    printf("[worker:%s pid=%d] basladi (mod=%s)\n", name, (int)pid, mode);
    fflush(stdout);

    if (strcmp(mode, "forever") == 0 || strcmp(mode, "steady") == 0) {
        for (;;) {
            sleep(1);
        }
    } else if (strcmp(mode, "normal") == 0) {
        int duration = parse_duration(dur_arg, 3);
        sleep((unsigned int)duration);
        printf("[worker:%s pid=%d] normal tamamlandi\n", name, (int)pid);
        fflush(stdout);
        return EXIT_SUCCESS;
    } else if (strcmp(mode, "error") == 0) {
        int duration = parse_duration(dur_arg, 2);
        sleep((unsigned int)duration);
        fprintf(stderr, "[worker:%s pid=%d] hata koduyla cikis yapiyor\n",
                name, (int)pid);
        return EXIT_FAILURE;
    } else if (strcmp(mode, "stubborn") == 0 || strcmp(mode, "ignore-term") == 0) {
        if (install_ignore_sigterm() == -1) {
            perror("sigaction");
            return EXIT_FAILURE;
        }
        for (;;) {
            if (got_sigterm) {
                got_sigterm = 0;
                printf("[worker:%s pid=%d] SIGTERM yoksayildi\n", name, (int)pid);
                fflush(stdout);
            }
            sleep(1);
        }
    } else if (strcmp(mode, "crash") == 0) {
        int duration = parse_duration(dur_arg, 2);
        sleep((unsigned int)duration);
        fprintf(stderr, "[worker:%s pid=%d] beklenmedik sinyal ile sonlaniyor\n",
                name, (int)pid);
        abort();
    } else {
        fprintf(stderr, "Bilinmeyen worker modu: %s\n", mode);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
}
