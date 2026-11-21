#include "logger.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LOG_LINE_MAX 512

/* Acik log dosyasinin tanimlayicisi; acik degilse -1. */
static int log_fd = -1;

/* write() bazen tum veriyi tek seferde yazmayabilir veya bir sinyalle
 * (EINTR) kesilebilir; bu yardimci fonksiyon tum tamponu yazana kadar dener. */
static int write_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

int log_open(const char *path)
{
    if (path == NULL)
        path = LOG_DEFAULT_PATH;

    /* O_APPEND: her satir dosyanin sonuna eklenir.
     * O_CLOEXEC: exec ile baslatilan worker'lar bu dosyayi miras almaz. */
    log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (log_fd == -1) {
        fprintf(stderr, "log_open: %s acilamadi: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

void log_event(const char *fmt, ...)
{
    char line[LOG_LINE_MAX];
    struct tm tm_now;
    time_t now = time(NULL);
    size_t len = 0;
    va_list ap;
    int n;

    /* Zaman damgasi: [YYYY-MM-DD HH:MM:SS] */
    if (localtime_r(&now, &tm_now) != NULL)
        len = strftime(line, sizeof line, "[%Y-%m-%d %H:%M:%S] ", &tm_now);

    /* Mesajin kendisi (printf bicimiyle). */
    va_start(ap, fmt);
    n = vsnprintf(line + len, sizeof line - len, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;

    /* Mesaj tampona sigmadiysa kirpilir; sona satir sonu eklenir. */
    len += (size_t)n;
    if (len > sizeof line - 2)
        len = sizeof line - 2;
    line[len++] = '\n';

    /* Log dosyasi acik degilse stderr'e yazilir, mesaj kaybolmaz. */
    if (write_all(log_fd != -1 ? log_fd : STDERR_FILENO, line, len) == -1)
        fprintf(stderr, "log_event: yazma hatasi: %s\n", strerror(errno));
}

void log_close(void)
{
    if (log_fd == -1)
        return;
    if (close(log_fd) == -1)
        fprintf(stderr, "log_close: %s\n", strerror(errno));
    log_fd = -1;
}
