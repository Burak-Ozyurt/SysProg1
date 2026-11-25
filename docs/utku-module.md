# Utku — Loglama, Durum Raporu ve Test Altyapısı

## Sorumluluk

Projenin loglama modülü, `SIGUSR1` ile tetiklenen durum raporu, derleme yapısı (Makefile), test altyapısı ve belgeler geliştirildi. Loglama fonksiyonları diğer iki modül tarafından ortak olarak kullanılır: Burak'ın ana programı ve Ahmet'in süreç yöneticisi tüm olayları `log_event()` üzerinden yazar.

İlgili dosyalar:

- `include/logger.h`
- `src/logger.c`
- `Makefile`
- `.gitignore`
- `tests/test_scenarios.sh`
- `README.md`
- `docs/TEST_RESULTS.md`, `docs/sample.log`

## Loglama Tasarımı

Modülün arayüzü dört fonksiyondan oluşur:

| Fonksiyon | Görev |
|---|---|
| `log_open(path)` | Log dosyasını açar; `NULL` verilirse varsayılan `watchdog.log` kullanılır. |
| `log_event(fmt, ...)` | `printf` biçiminde, zaman damgalı tek bir log satırı yazar. |
| `log_status_report()` | Tüm worker'ların durumunu loga yazar (`SIGUSR1` raporu). |
| `log_close()` | Log dosyasını kapatır. |

Her satır şu biçimdedir:

```text
[YYYY-MM-DD HH:MM:SS] OLAY ayrintilar
```

Tasarımda alınan kararlar:

- **`O_APPEND`:** Dosya ekleme modunda açılır; her satır dosyanın sonuna yazılır ve önceki kayıtlar korunur.
- **`O_CLOEXEC`:** Log dosyasının tanımlayıcısı `exec()` sırasında otomatik kapanır. Böylece başlatılan worker'lar watchdog'un log dosyasını miras almaz ve kaynak sızıntısı oluşmaz.
- **Tek `write()` çağrısıyla satır yazma:** Zaman damgası ve mesaj önce bir tampona (`snprintf`/`vsnprintf`) hazırlanır, ardından tek seferde yazılır. Satırlar parça parça yazılmadığı için iç içe geçmez.
- **`write_all()` yardımcı fonksiyonu:** `write()` verinin tamamını tek seferde yazmayabilir ya da bir sinyalle (`EINTR`) kesilebilir. Yardımcı fonksiyon bu durumda kalan kısmı yazmaya devam eder. Bu, projede sinyallerin sık geldiği düşünüldüğünde önemlidir.
- **Taşma koruması:** Satır tamponu 512 bayttır; daha uzun mesajlar kırpılır, satır sonu karakteri her zaman eklenir.
- **Yedek çıktı:** Log dosyası açık değilse mesajlar kaybolmasın diye standart hata çıkışına (`stderr`) yazılır.
- **Hata yönetimi:** `open()`, `write()` ve `close()` hataları `strerror(errno)` ile `stderr`'e bildirilir.
- **Zaman:** `localtime_r()` kullanılır; `localtime()`'ın aksine paylaşılan statik bir alana yazmaz.

**Sinyal güvenliği:** `log_event()` dosya işlemleri ve biçimlendirme yaptığı için async-signal-safe değildir. Bu yüzden sinyal işleyicilerinden çağrılmaz; yalnızca ana akıştan çağrılır. Sinyal işleyicileri sadece bayrak set eder, loglama ana döngüde yapılır (bkz. `docs/burak-module.md`).

## Durum Raporu (SIGUSR1)

Watchdog `SIGUSR1` aldığında ana döngü `log_status_report()` fonksiyonunu çağırır. Fonksiyon, süreç yöneticisinin `pm_get_workers()` ile sağladığı worker listesine salt okunur erişir ve şunları yazar:

- Toplam ve çalışan worker sayısı
- Çalışan worker'lar için ad, PID, durum ve yeniden başlatma sayısı
- Çalışmayan worker'lar için durum (`COMPLETED`, `FAILED`, `TERMINATED`) ve son çıkış nedeni

Örnek çıktı:

```text
[2026-11-24 15:34:17] STATUS rapor: toplam=4 calisan=2
[2026-11-24 15:34:17] STATUS worker-1 pid=1744 state=RUNNING restarts=0/3
[2026-11-24 15:34:17] STATUS worker-2 pid=- state=COMPLETED restarts=0/3 last_exit=NORMAL
[2026-11-24 15:34:17] STATUS worker-3 pid=- state=FAILED restarts=3/3 last_exit=SIGNAL
[2026-11-24 15:34:17] STATUS worker-4 pid=1747 state=RUNNING restarts=0/3
```

Durum ve çıkış nedeni metinleri Ahmet'in `pm_state_to_string()` ve `pm_exit_reason_to_string()` fonksiyonlarından alınır.

## Derleme Yapısı

`Makefile` iki çalıştırılabilir dosya üretir: `watchdog` ve `worker`.

| Hedef | Açıklama |
|---|---|
| `make` / `make all` | `watchdog` ve `worker` derlenir. |
| `make clean` | Derleme çıktıları silinir. |

Derleme bayrakları: `-Wall -Wextra -std=c11 -D_XOPEN_SOURCE=700 -Iinclude`. `_XOPEN_SOURCE=700`, POSIX.1-2008 ve XSI arayüzlerini (`sigaction`, `setitimer`, `pselect`, `clock_gettime` vb.) standart C11 ile birlikte kullanabilmek için tanımlanmıştır. Proje bu bayraklarla uyarısız derlenmektedir.

`.gitignore`, derleme çıktılarını (`watchdog`, `worker`, `*.o`) ve çalışma sırasında oluşan `watchdog.log` dosyasını depo dışında tutar. Örnek log, `docs/sample.log` olarak ayrıca saklanmıştır.

## Test Altyapısı

`tests/test_scenarios.sh`, PDF Bölüm 7'deki senaryoları otomatik doğrulayan bir Bash scriptidir. 17 kontrol içerir ve sonucu `PASS` / `FAIL` / `SKIP` olarak raporlar. Herhangi bir kontrol başarısız olursa script sıfırdan farklı bir kodla çıkar.

Script iki aşamadan oluşur:

1. **Worker modlarının doğrulanması:** Watchdog'un ayırt etmesi gereken üç çıkış türünü (normal, hata kodu, sinyal) worker'ın gerçekten ürettiği, watchdog'dan bağımsız olarak kontrol edilir.
2. **Watchdog senaryoları:** Her senaryo için watchdog temiz bir log dosyasıyla başlatılır, gerekli sinyaller gönderilir ve logda beklenen satırlar aranır.

Kullanılan yardımcı fonksiyonlar:

| Fonksiyon | Görev |
|---|---|
| `start_watchdog` | Watchdog'u arka planda başlatır, `WATCHDOG START` satırını bekler. |
| `stop_watchdog` | `SIGTERM` gönderir, zaman aşımı dahil 8 saniyeye kadar bekler; kapanmazsa durdurur ve kalan worker'ları temizler. |
| `wait_for_log` | Logda bir ifade görünene kadar belirli bir süre bekler (sabit `sleep` yerine). |
| `worker_pid` | Logdaki `START` satırlarından bir worker'ın güncel PID'ini bulur. |
| `check_shutdown` | `SIGTERM` ve `SIGINT` kapanış testlerinde ortak kontrolleri yapar. |
| `zombie_children` | Watchdog'un çocukları arasında zombie (`Z`) süreç sayar. |

Testlerde dikkat edilen noktalar:

- **PID doğrulama:** PDF'in istediği şekilde, bir worker'a `SIGKILL` göndermeden önce PID'in gerçekten bir `worker` sürecine ait olduğu `ps -o comm=` ile doğrulanır.
- **Zaman aşımının ölçülmesi:** Kapanış süresi ölçülür; `SIGTERM`'i yok sayan worker'ın yaklaşık 5 saniye sonra `SIGKILL` ile sonlandırılması beklenir.
- **Zombie kontrolü:** Çalışma sırasında ve kapanıştan sonra `defunct` süreç kalmadığı `ps` ile kontrol edilir.
- **Testlerin birbirini etkilememesi:** Her senaryo kendi log dosyasıyla başlar ve sonunda geride çalışan süreç bırakmaz.

Elle yapılan Ctrl+C testi dahil tüm sonuçlar `docs/TEST_RESULTS.md` dosyasında belgelenmiştir.

## Kullanılan Temel Sistem Çağrıları ve Fonksiyonlar

| Çağrı | Kullanım amacı |
|---|---|
| `open()` | Log dosyasını `O_WRONLY \| O_CREAT \| O_APPEND \| O_CLOEXEC` ile açmak. |
| `write()` | Log satırını dosyaya yazmak (`EINTR` ve kısmi yazmaya karşı döngüyle). |
| `close()` | Log dosyasını kapatmak. |
| `time()`, `localtime_r()` | Zaman damgası için güncel zamanı almak. |
| `strftime()` | Zaman damgasını biçimlendirmek. |
| `vsnprintf()` | Değişken argümanlı mesajı taşma olmadan tampona yazmak. |
| `strerror()` | Hata mesajlarını okunur hale getirmek. |

## Yapılan Doğrulamalar

Ubuntu 26.04 (WSL 2) üzerinde aşağıdaki kontroller yapıldı:

- `logger.c` küçük bir test programıyla ayrı olarak denendi; zaman damgalı satırların doğru biçimde yazıldığı görüldü.
- `log_status_report()` gerçek worker'larla denendi; çalışan ve biten worker'ların durumu doğru raporlandı.
- Proje `make` ile uyarısız derlendi.
- `tests/test_scenarios.sh` tüm modüller birleştikten sonra çalıştırıldı: **17 PASS, 0 FAIL, 0 SKIP**.
- Ctrl+C testi terminalde elle yapıldı ve sonuçları belgelendi.

Ayrıntılı sonuçlar için `docs/TEST_RESULTS.md`, örnek log için `docs/sample.log` dosyasına bakılabilir.
