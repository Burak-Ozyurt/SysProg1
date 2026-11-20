#ifndef LOGGER_H
#define LOGGER_H

/** Varsayilan log dosyasi yolu. */
#define LOG_DEFAULT_PATH "watchdog.log"

/**
 * @brief Log dosyasini ekleme (append) modunda acar.
 * @param path Log dosyasi yolu; NULL ise LOG_DEFAULT_PATH kullanilir.
 * @return Basariliysa 0, hata durumunda -1.
 */
int log_open(const char *path);

/**
 * @brief Zaman damgali bir olay satirini log dosyasina yazar.
 *
 * printf bicimini kullanir. Ornek:
 *   log_event("START %s pid=%d", name, pid);
 * Cikti: [2026-11-20 15:40:12] START worker-1 pid=1234
 *
 * Sinyal isleyicisi icinden CAGRILMAMALIDIR; yalnizca ana akistan cagrilir.
 */
void log_event(const char *fmt, ...);

/**
 * @brief SIGUSR1 ile tetiklenen durum raporunu yazar.
 *
 * pm_get_workers() ile tum worker'larin adini, PID'ini, durumunu ve
 * yeniden baslatma sayisini log dosyasina yazar.
 */
void log_status_report(void);

/**
 * @brief Log dosyasini kapatir.
 */
void log_close(void);

#endif /* LOGGER_H */
