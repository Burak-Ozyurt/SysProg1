#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

/** Maksimum izlenebilecek alt surec (worker) sayisi. */
#define PM_MAX_WORKERS 8

/** Beklenmedik kapanmada worker basina izin verilen azami yeniden baslatma sayisi. */
#define PM_MAX_RESTARTS 3

/** Varsayilan periyodik kontrol suresi (saniye). */
#define PM_DEFAULT_CHECK_INTERVAL_SEC 5

/** Kontrollu kapanis sirasinda SIGKILL oncesi bekleme suresi (saniye). */
#define PM_SHUTDOWN_TIMEOUT_SEC 5

/** Worker adi ve komut satiri argumanlari icin sabit tampon boyutlari. */
#define PM_NAME_MAX 64
#define PM_PATH_MAX 256
#define PM_MAX_ARGS 8
#define PM_ARG_MAX 64

/**
 * @brief Bir alt surecin yasam dongusu durumunu temsil eder.
 */
typedef enum {
    WORKER_STATE_NOT_STARTED = 0, /**< Henuz baslatilmadi. */
    WORKER_STATE_RUNNING,         /**< Su anda calisiyor. */
    WORKER_STATE_COMPLETED,       /**< Normal cikis (exit code 0) ile tamamlandi; yeniden baslatilmaz. */
    WORKER_STATE_FAILED,          /**< Beklenmedik sekilde kapandi ve yeniden baslatma sinirina ulasti. */
    WORKER_STATE_TERMINATED       /**< Watchdog kapanisi sirasinda sonlandirildi. */
} WorkerState;

/**
 * @brief Alt surecin sonlanma nedenini siniflandirir.
 */
typedef enum {
    WORKER_EXIT_NONE = 0, /**< Henuz sonlanmadi. */
    WORKER_EXIT_NORMAL,   /**< exit(0) veya return 0 ile normal sonlandi. */
    WORKER_EXIT_ERROR,    /**< Sifir disi bir hata koduyla sonlandi. */
    WORKER_EXIT_SIGNAL    /**< Bir sinyal tarafindan sonlandirildi. */
} WorkerExitReason;

/**
 * @brief Tek bir alt surece ait tum takip ve calistirma bilgilerini tutar.
 */
typedef struct {
    int id;                                 /**< 0 tabanli mantiksal worker kimligi. */
    char name[PM_NAME_MAX];                 /**< Okunabilir worker adi (orn. "worker-1"). */
    char exec_path[PM_PATH_MAX];            /**< Calistirilacak ikili dosya yolu (orn. "./worker"). */
    char args[PM_MAX_ARGS][PM_ARG_MAX];     /**< execv icin arguman kopyalari (args[0] program adi). */
    int argc;                               /**< Gecerli arguman sayisi. */
    pid_t pid;                              /**< Calisan surecin PID degeri; calismiyorsa -1. */
    WorkerState state;                      /**< Guncel yasam dongusu durumu. */
    WorkerExitReason last_exit_reason;      /**< Son sonlanma sinifi. */
    int last_exit_code;                     /**< WIFEXITED durumunda cikis kodu; aksi halde -1. */
    int last_term_signal;                   /**< WIFSIGNALED durumunda sinyal numarasi; aksi halde 0. */
    int restart_count;                      /**< Ilk baslatma haric yeniden baslatma sayaci (0..PM_MAX_RESTARTS). */
    time_t started_at;                      /**< Son baslatilma zamani. */
    time_t last_exit_at;                    /**< Son sonlanma zamani. */
} WorkerProcess;

/**
 * @brief Surec yoneticisinin ic durumunu sifirlar ve baslatir.
 * @return Basariliysa 0, hata varsa -1.
 */
int pm_init(void);

/**
 * @brief Yeni bir worker tanimi ekler (henuz baslatmaz).
 * @param name Worker icin okunabilir ad (NULL olamaz).
 * @param exec_path Calistirilacak dosya yolu (NULL olamaz).
 * @param argv NULL ile sonlanan arguman dizisi (NULL ise yalnizca exec_path kullanilir).
 * @return Eklenen worker'in id degeri (>= 0), hata durumunda -1.
 */
int pm_add_worker(const char *name, const char *exec_path, const char *const argv[]);

/**
 * @brief Kayitli tum worker'lari fork/exec ile baslatir.
 * @return Basariliysa 0, herhangi biri baslatilamazsa -1.
 */
int pm_start_all(void);

/**
 * @brief Belirli bir worker'i index (id) ile baslatir.
 * @param worker_id Worker kimligi.
 * @return Basariliysa 0, hata durumunda -1.
 */
int pm_start_worker(int worker_id);

/**
 * @brief SIGCHLD sonrasinda sonlanan tum alt surecleri waitpid(WNOHANG) ile toplar.
 *
 * Ayni anda birden fazla surecin sonlanmis olma ihtimaline karsi dongu ile tum
 * bekleyen surecleri toplar; cikis nedenini siniflandirir. Kapanis modu kapaliysa
 * ve beklenmedik cikis olduysa PM_MAX_RESTARTS sinirina kadar yeniden baslatir.
 *
 * @return Toplanan alt surec sayisi (>= 0), waitpid hatasinda -1.
 */
int pm_reap_children(void);

/**
 * @brief Watchdog kapanis modunu ayarlar.
 *
 * true yapildiginda yeni surec baslatma ve otomatik yeniden baslatma durdurulur.
 *
 * @param shutting_down Kapanis basladiysa true, normal calismada false.
 */
void pm_set_shutting_down(bool shutting_down);

/**
 * @brief Watchdog'un kapanis modunda olup olmadigini dondurur.
 * @return Kapanis modundaysa true, aksi halde false.
 */
bool pm_is_shutting_down(void);

/**
 * @brief Calisan tum alt sureclere kontrollu sonlandirma icin SIGTERM gonderir.
 * @return Sinyal gonderilen surec sayisi (>= 0), hata varsa -1.
 */
int pm_terminate_all(void);

/**
 * @brief Zaman asimi sonrasinda hala calisan alt sureclere SIGKILL gonderir ve toplar.
 * @return Zorla sonlandirilan surec sayisi (>= 0), hata varsa -1.
 */
int pm_force_kill_remaining(void);

/**
 * @brief Su anda WORKER_STATE_RUNNING durumundaki aktif alt surec sayisini dondurur.
 * @return Aktif worker sayisi.
 */
int pm_active_count(void);

/**
 * @brief setitimer() ile periyodik kontrol zamanlayicisini (SIGALRM) baslatir.
 * @param interval_sec Kontrol araligi (saniye; 0 verilirse PM_DEFAULT_CHECK_INTERVAL_SEC kullanilir).
 * @return Basariliysa 0, hata durumunda -1.
 */
int pm_start_periodic_timer(unsigned int interval_sec);

/**
 * @brief Periyodik kontrol zamanlayicisini durdurur.
 */
void pm_stop_periodic_timer(void);

/**
 * @brief SIGALRM alindiginda ana donguden cagrilarak aktif alt sureclerin durumunu kontrol eder.
 * @return Hala aktif gorunen worker sayisi (>= 0), hata varsa -1.
 */
int pm_periodic_check(void);

/**
 * @brief Utku'nun durum raporu ve loglama modulu icin kayitli worker dizisine salt-okunur erisim saglar.
 * @param count Kayitli toplam worker sayisinin yazilacagi isaretci (NULL olabilir).
 * @return WorkerProcess dizisinin baslangic adresi.
 */
const WorkerProcess *pm_get_workers(size_t *count);

/**
 * @brief WorkerState degerini okunabilir metne cevirir.
 * @param state Durum enum degeri.
 * @return Sabit durum metni (orn. "RUNNING").
 */
const char *pm_state_to_string(WorkerState state);

/**
 * @brief WorkerExitReason degerini okunabilir metne cevirir.
 * @param reason Sonlanma nedeni enum degeri.
 * @return Sabit aciklama metni (orn. "NORMAL", "ERROR", "SIGNAL").
 */
const char *pm_exit_reason_to_string(WorkerExitReason reason);

/**
 * @brief Surec yoneticisi kaynaklarini temizler.
 */
void pm_cleanup(void);

#endif /* PROCESS_MANAGER_H */
