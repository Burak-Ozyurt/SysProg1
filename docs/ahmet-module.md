# Ahmet — Alt Süreç Yönetimi, Yeniden Başlatma ve Periyodik Kontrol

## Sorumluluk

Alt süreçlerin (`worker`) oluşturulması, PID ve yaşam döngüsü durumlarının tutulması, `SIGCHLD` sonrası sonlanan süreçlerin toplanıp sınıflandırılması, beklenmedik kapanmalarda sınırlı yeniden başlatma, kontrollü kapanış sinyal işlevleri ve periyodik kontrol zamanlayıcısı geliştirildi. Ayrıca test senaryolarında kullanılan `worker` programı yazıldı ve hata durumundaki temizlik akışı (`cleanup_after_error`) süreç yöneticisiyle birleştirildi.

İlgili dosyalar:

- `include/process_manager.h`
- `src/process_manager.c`
- `workers/worker.c`
- `src/main.c` (`cleanup_after_error` içinde `pm_force_kill_remaining` entegrasyonu)

## Veri Yapısı ve Süreç Başlatma Tasarımı

Süreç yöneticisinin ortak arayüzü `include/process_manager.h` içinde tanımlanmıştır. Tüm modüller bu yapı ve fonksiyonlar üzerinden haberleşir:

- **Sabit sınırlar:** `PM_MAX_WORKERS` (`8`), `PM_MAX_RESTARTS` (`3`), `PM_DEFAULT_CHECK_INTERVAL_SEC` (`5` sn), `PM_SHUTDOWN_TIMEOUT_SEC` (`5` sn).
- **Durum ve çıkış türleri:**
  - `WorkerState`: `WORKER_STATE_NOT_STARTED`, `WORKER_STATE_RUNNING`, `WORKER_STATE_COMPLETED` (normal çıkış), `WORKER_STATE_FAILED` (yeniden başlatma sınırı aşıldı veya kayıp), `WORKER_STATE_TERMINATED` (kapanışta sonlandırıldı).
  - `WorkerExitReason`: `WORKER_EXIT_NONE`, `WORKER_EXIT_NORMAL`, `WORKER_EXIT_ERROR`, `WORKER_EXIT_SIGNAL`.
- **Sabit bellek kullanımı:** Her `WorkerProcess` kaydı; `id`, `name`, `exec_path`, argüman tamponları (`args[PM_MAX_ARGS][PM_ARG_MAX]`, `argc`), `pid`, `state`, son çıkış bilgileri (`last_exit_reason`, `last_exit_code`, `last_term_signal`), `restart_count` ve zaman damgalarını (`started_at`, `last_exit_at`) sabit dizilerde tutar. Dinamik bellek (`malloc`/`free`) kullanılmadığı için bellek sızıntısı riski yoktur.

### Alt Süreç Başlatma (`pm_start_worker`, `pm_start_all`)

- `fork()` öncesinde `shutting_down` durumu, argüman sayısı ve `access(w->exec_path, X_OK)` ile çalıştırılabilirlik doğrulanır.
- Çocuk süreçte (`pid == 0`), ebeveyn ana döngüsünden `sigprocmask()` ile miras alınan engellenmiş sinyal maskesi `sigemptyset()` + `sigprocmask(SIG_SETMASK, ...)` ile sıfırlanır; böylece worker'lar `SIGTERM`/`SIGINT` bloklu halde başlamaz.
- Maske kurulumu veya `execv()` başarısız olursa `stdio` tamponlarını bozmamak için sinyal-güvenli `write(STDERR_FILENO, ...)` ile hata yazılır ve `_exit(127)` ile çıkılır.
- Ebeveyn süreçte dönen PID kaydedilir, durum `WORKER_STATE_RUNNING` yapılır ve Utku'nun `log_event()` fonksiyonuyla `START` kaydı düşülür.
- `pm_start_all()` sırasında bir worker başlatılamazsa, o ana kadar başlatılmış olan süreçler `pm_force_kill_remaining()` ile geri temizlenir.

## Sonlanma Takibi ve Sınırlı Yeniden Başlatma

Ana döngü `SIGCHLD` aldığında `pm_reap_children()` fonksiyonunu çağırır:

- **Çoklu sonlanma desteği:** Aynı `SIGCHLD` bildirimi gelmeden birden fazla alt süreç kapanmış olabileceği için `waitpid(-1, &status, WNOHANG)` döngü içinde (`pid > 0` oldukça) çalıştırılır. `EINTR` durumunda tekrar denenir; `ECHILD` veya `0` dönüşünde döngü sonlanır. Böylece zombie (`<defunct>`) süreç kalmaz.
- **Çıkış sınıflandırması (`record_worker_exit`):**
  - `WIFEXITED(status)` ve `WEXITSTATUS(status) == 0` → `WORKER_EXIT_NORMAL`, `WORKER_STATE_COMPLETED`.
  - `WIFEXITED(status)` ve `WEXITSTATUS(status) != 0` → `WORKER_EXIT_ERROR`, çıkış kodu kaydedilir.
  - `WIFSIGNALED(status)` → `WORKER_EXIT_SIGNAL`, sonlandıran sinyal (`WTERMSIG`) kaydedilir.
  - Her durumda `pid = -1` ve `last_exit_at` güncellenip `EXIT ... reason=...` loglanır.
- **Sınırlı yeniden başlatma (`maybe_restart_worker`):**
  - İlk başlatma yeniden başlatma sayılmaz (`restart_count = 0`).
  - Yalnızca beklenmedik kapanmalarda (`WORKER_EXIT_ERROR` veya `WORKER_EXIT_SIGNAL`) ve `shutting_down == false` iken yeniden başlatma değerlendirilir.
  - `restart_count < PM_MAX_RESTARTS` (3) ise sayaç artırılır, `RESTART <ad> (<sayac>/3)` loglanır ve `pm_start_worker()` ile yeni PID başlatılır.
  - 3 denemeden sonra 4. kez beklenmedik kapanma olursa durum `WORKER_STATE_FAILED` olarak bırakılır, `GIVE_UP` loglanır ve bir daha deneme yapılmaz.
  - Normal tamamlanan (`WORKER_EXIT_NORMAL`) veya kapanış modundayken sonlanan süreçler yeniden başlatılmaz.

## Kontrollü Kapanış ve Periyodik Kontrol

- **Kontrollü kapatma (`pm_terminate_all`, `pm_force_kill_remaining`):**
  - `pm_terminate_all()` kapanış modunu (`shutting_down = true`) etkinleştirir ve `WORKER_STATE_RUNNING` durumundaki tüm worker'lara `SIGTERM` gönderir (`ESRCH` hatası süreç zaten çıktığı için güvenli şekilde atlanır).
  - Zaman aşımı dolduğunda Burak'ın kapanış akışı `pm_force_kill_remaining()` fonksiyonunu çağırır. Bu fonksiyon önce bekleyen çıkışları `pm_reap_children()` ile toplar, hâlâ çalışanlara `SIGKILL` gönderip `waitpid(w->pid, &status, 0)` ile anında toplar ve son bir tarama daha yapar.
- **Periyodik kontrol (`pm_start_periodic_timer`, `pm_stop_periodic_timer`, `pm_periodic_check`):**
  - `pm_start_periodic_timer()`, `setitimer(ITIMER_REAL, ...)` ile varsayılan olarak 5 saniyede bir (`PM_DEFAULT_CHECK_INTERVAL_SEC`) `SIGALRM` üreten aralık zamanlayıcısını kurar.
  - `SIGALRM` sonrası ana döngüden çağrılan `pm_periodic_check()`, önce bekleyen çıkışları toplar, ardından çalışan her worker için `kill(w->pid, 0)` ile canlılık kontrolü yapar ve `PERIODIC_CHECK aktif=... toplam=...` özetini loglar.
  - `pm_stop_periodic_timer()` zamanlayıcıyı sıfırlar; `pm_cleanup()` ve kapanış akışı tarafından çağrılır.
- **Durum bilgisi erişimi:** `pm_get_workers()`, `pm_active_count()`, `pm_state_to_string()` ve `pm_exit_reason_to_string()` fonksiyonları Utku'nun `SIGUSR1` durum raporu ve Burak'ın ana döngüsü tarafından kullanılır.

## Hata Yönetimi ve Kaynak Temizliği

- Başlatılmamış durumda (`!initialized`) çağrılan yönetici fonksiyonları `-1` döndürür; `pm_init()` tekrar çağrılırsa önce `pm_cleanup()` ile mevcut kaynakları temizler.
- `pm_cleanup()` zamanlayıcıyı durdurur, hâlâ çalışan worker kalmışsa `pm_force_kill_remaining()` çalıştırır ve ek `waitpid(-1, NULL, WNOHANG)` taraması yaparak geride yetim/zombie süreç kalmamasını garanti eder.
- `src/main.c` içindeki `cleanup_after_error()` da doğrudan `pm_force_kill_remaining()` kullanacak şekilde güncellenerek hata temizliğinde `WorkerProcess` durum ve PID kayıtlarının tutarlı kalması sağlanmıştır.

## Test Amaçlı Worker Programı (`workers/worker.c`)

`Makefile` tarafından `worker` ikilisi olarak derlenen test programı komut satırından `<worker_adi> [mod] [sure_sn]` alır ve tüm senaryoları üretir:

| Mod | Davranış | Kullanıldığı senaryo |
|---|---|---|
| `forever` (`steady`) | Sinyal gelene kadar çalışır; `SIGTERM` ile sonlanır. | Normal çalışma, `SIGUSR1`, `SIGKILL`, `SIGTERM`/Ctrl+C |
| `normal` | Belirtilen süre (varsayılan 3 sn) sonunda `exit(0)` ile çıkar. | Normal tamamlanma (yeniden başlatılmaz) |
| `error` | Belirtilen süre (varsayılan 2 sn) sonunda `exit(1)` ile çıkar. | Hata koduyla çıkış ve yeniden başlatma |
| `stubborn` (`ignore-term`) | `sigaction()` ile `SIGTERM`'i yakalayıp yok sayar; `SIGKILL` ile biter. | Kapanış zaman aşımı ve kontrollü `SIGKILL` |
| `crash` | Belirtilen süre (varsayılan 2 sn) sonunda `abort()` (`SIGABRT`) üretir. | Sinyalle beklenmedik sonlanma ve 3 deneme sınırı |

## Kullanılan Temel Sistem Çağrıları

| Çağrı | Kullanım amacı |
|---|---|
| `fork()` | Yeni alt süreç (`worker`) oluşturmak. |
| `execv()` | Alt süreçte `worker` programını çalıştırmak. |
| `_exit()` | `execv()` veya maske sıfırlama hatasında çocuk süreci güvenle sonlandırmak. |
| `waitpid()` | `WNOHANG` döngüsüyle ve `SIGKILL` sonrası sonlanan süreçleri toplamak, zombie önlemek. |
| `kill()` | Worker'lara `SIGTERM`/`SIGKILL` göndermek ve `kill(pid, 0)` ile canlılık kontrolü yapmak. |
| `setitimer()` | `ITIMER_REAL` üzerinden periyodik `SIGALRM` zamanlayıcısını başlatmak ve durdurmak. |
| `sigemptyset()`, `sigprocmask()` | Çocuk süreçte `execv()` öncesi miras alınan sinyal maskesini sıfırlamak. |
| `sigaction()` | `worker.c` içinde `stubborn` modunda `SIGTERM`'i yakalamak. |
| `access()` | `fork()` öncesinde çalıştırılacak ikili dosyanın varlığını ve yürütme iznini (`X_OK`) doğrulamak. |
| `write()` | Çocuk süreçte `execv()` hatasında sinyal-güvenli hata mesajı yazmak. |

## Yapılan Doğrulamalar

Ubuntu ortamında (`gcc -Wall -Wextra -std=c11 -D_XOPEN_SOURCE=700`) aşağıdaki kontroller gerçekleştirildi:

- `make` ile `watchdog` ve `worker` programlarının uyarısız derlendiği doğrulandı.
- Tüm `worker` modları (`normal`, `error`, `crash`, `forever`, `stubborn`) ayrı ayrı test edildi.
- Birim test sürücüleriyle `pm_reap_children()` çoklu toplama + `NORMAL`/`ERROR`/`SIGNAL` ayrımı, 3 yeniden başlatma sonrası `GIVE_UP` sınırı, kapanış sırasında yeniden başlatmanın engellenmesi, `pm_terminate_all()` + `pm_force_kill_remaining()`, `setitimer`/`SIGALRM` ile `pm_periodic_check()` ve kısmi hata/temizlik senaryoları (`ECHILD` ile sıfır zombie) doğrulandı.
- Ekibin birleşik test akışı olan `./tests/test_scenarios.sh` Ubuntu üzerinde çalıştırılarak doğrulandı (birleşik çıktılar için `docs/TEST_RESULTS.md` ve `docs/sample.log` dosyalarına bakınız).
