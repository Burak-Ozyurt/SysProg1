# Burak — Sinyal Yönetimi ve Ana Program

## Sorumluluk

Sinyal yönetimi altyapısı, ana kontrol döngüsü ve kontrollü kapanış akışı geliştirildi. Ahmet’in süreç yönetimi ve Utku’nun loglama modülleri ana programa bağlandı.

İlgili dosyalar:

- `include/signal_manager.h`
- `src/signal_manager.c`
- `src/main.c`

## Sinyal Yönetimi Tasarımı

Sinyal işleyicileri `sigaction()` ile kurulmaktadır. İşleyiciler yalnızca `volatile sig_atomic_t` türündeki ilgili bayrağı `1` yapar. Loglama, süreç oluşturma ve sonlanan süreçleri toplama işlemleri ana program akışında gerçekleştirilir.

| Sinyal | Ana programdaki işlem |
|---|---|
| `SIGINT`, `SIGTERM` | Kontrollü kapanışı başlatır. |
| `SIGUSR1` | Worker durum raporunu loga yazdırır. |
| `SIGCHLD` | Sonlanan alt süreçlerin toplanmasını tetikler. |
| `SIGALRM` | Periyodik süreç kontrolünü tetikler. |

Kurulumda `SA_RESTART`, `SIGCHLD` için ayrıca `SA_NOCLDSTOP` kullanılır. Bildirim bayrakları işleyiciler kurulmadan önce sıfırlanır.

## Ana Kontrol Döngüsü

Başlangıçta log dosyası açılır, süreç yöneticisi hazırlanır ve worker’lar başlatılır. Ardından periyodik zamanlayıcı etkinleştirilir.

İlgili sinyaller, bayraklar kontrol edilirken `sigprocmask()` ile engellenir. Normal çalışmada `sigsuspend()`, sinyalleri geçici olarak açarak bekler. Böylece bayrak kontrolü ile beklemeye geçiş arasında sinyalin işlenip programın gereksiz yere uyuması önlenir.

Ana döngüde:

- `SIGCHLD` bildirimi için `pm_reap_children()` çağrılır.
- `SIGUSR1` bildirimi için `log_status_report()` çağrılır.
- `SIGALRM` bildirimi için `pm_periodic_check()` çağrılır.

Bu fonksiyonların uygulamaları ilgili ekip üyelerinin modüllerindedir.

## Kontrollü Kapanış ve Zaman Aşımı

İlk kapanış isteğinde süreç yöneticisi kapanış moduna alınır, periyodik zamanlayıcı durdurulur ve çalışan worker’lara `SIGTERM` gönderilir. Kapanış modunda yeniden başlatma yapılmaz.

Kapanış için 5 saniyelik son süre `clock_gettime(CLOCK_MONOTONIC, ...)` ile hesaplanır. Bu saat, sistemin tarih/saat ayarındaki değişikliklerden etkilenmez. Kapanış sırasında `pselect()` ile kalan süre kadar beklenir; gelen sinyaller de işlenir. Tekrarlanan kapanış istekleri süreyi yeniden başlatmaz.

Süre sonunda hâlâ çalışan worker’lar `pm_force_kill_remaining()` ile `SIGKILL` gönderilerek sonlandırılıp toplanır. Worker’lar toplandıktan sonra kaynaklar temizlenir ve log dosyası kapatılır.

Kapanış süresi için `alarm()` kullanılmadığından periyodik kontrolün `SIGALRM` zamanlayıcısıyla çakışma oluşmaz.

## Kullanılan Temel Sistem Çağrıları

| Çağrı | Kullanım amacı |
|---|---|
| `sigaction()` | Sinyal işleyicilerini kurmak. |
| `sigprocmask()` | Sinyal maskesini yönetmek ve çıkışta eski maskeyi geri yüklemek. |
| `sigsuspend()` | Normal çalışmada sinyalleri atomik olarak açıp beklemek. |
| `pselect()` | Kapanış sırasında sinyal beklemeyi kalan süreyle sınırlamak. |
| `clock_gettime()` | Monoton saat üzerinden kapanış süresini hesaplamak. |
| `getpid()` | Watchdog PID bilgisini göstermek ve loglamak. |

Worker oluşturma, sinyal gönderme ve süreç toplama işlemleri süreç yöneticisinin fonksiyonları üzerinden yürütülür.

## Yapılan Doğrulamalar

Ubuntu üzerinde aşağıdaki kontroller gerçekleştirildi:

- GCC ile sinyal modülü derlendi; geçici test programında `raise()` kullanılarak beş sinyalin ilgili bayrağı değiştirdiği doğrulandı.
- Proje `make` ile derlendi.
- Normal tamamlanan ve sinyalle sonlanan worker’ların toplanıp loglandığı görüldü.
- `SIGUSR1` ile PID ve durum raporu doğrulandı.
- Ctrl+C ve yalnızca watchdog’a gönderilen `SIGTERM` ile kapanış ayrı ayrı denendi.
- `stubborn` worker’ın `SIGTERM`i yok saydığı, 5 saniye sonunda `SIGKILL` ile sonlandırılıp toplandığı doğrulandı.
- Periyodik kontrol kayıtlarının 5 saniye arayla oluştuğu görüldü.
- Kapanış başladıktan sonra periyodik kontrol ve yeniden başlatma kaydı oluşmadığı doğrulandı.
- Zaman aşımı testinde watchdog’un çıkış kodu `0`, kapanış süresi 5 saniye olarak gözlendi.

Son birleşik test sonuçları ayrıca `docs/TEST_RESULTS.md` içinde belgelenecektir.