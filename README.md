# Watchdog: POSIX Sinyal Tabanlı Süreç İzleme ve Yönetim Sistemi

Sakarya Üniversitesi Yazılım Mühendisliği, Sistem Programlama 2026–2027 Güz Dönemi Proje 1.

`watchdog`, C dilinde ve POSIX sistem çağrılarıyla yazılmış bir süreç denetleyicisidir. Başlattığı alt süreçleri (worker) izler, beklenmedik şekilde sonlananları sınırlı sayıda yeniden başlatır ve kullanıcının gönderdiği sinyallere göre durum raporu üretir veya kontrollü şekilde kapanır.

## Özellikler

- `fork()` ve `execv()` ile birden fazla worker başlatma
- `SIGCHLD` ve `waitpid()` ile sonlanan worker'ları tespit etme ve toplama (zombie süreç bırakmadan)
- Normal çıkış, hata koduyla çıkış ve sinyalle sonlanmayı ayırt etme
- Beklenmedik şekilde sonlanan worker'ı en fazla **3 kez** yeniden başlatma (ilk başlatma sayılmaz)
- `SIGUSR1` ile worker'ların PID ve durumlarını log dosyasına yazma
- `SIGINT` (Ctrl+C) ve `SIGTERM` ile kontrollü kapanış: worker'lara `SIGTERM`, 5 saniyelik zaman aşımından sonra `SIGKILL`
- `setitimer()` / `SIGALRM` ile 5 saniyede bir periyodik kontrol
- Tüm önemli olayların zaman damgasıyla `watchdog.log` dosyasına yazılması
- Async-signal-safe sinyal işleyicileri: işleyiciler yalnızca bayrak set eder, asıl işlemler ana döngüde yapılır

## Gereksinimler

- Linux (veya Windows'ta WSL 2 / Ubuntu)
- GCC
- GNU Make

Ubuntu'da kurulum:

```bash
sudo apt update
sudo apt install -y build-essential git
```

## Derleme

Depo kök dizininde:

```bash
make
```

Bu komut `watchdog` ve `worker` çalıştırılabilir dosyalarını üretir. Proje `-Wall -Wextra` bayraklarıyla uyarısız derlenir. Derleme çıktılarını silmek için:

```bash
make clean
```

## Çalıştırma

Program depo kök dizininden başlatılmalıdır (worker `./worker` yolundan çalıştırılır):

```bash
./watchdog
```

Program başlarken kendi PID'ini ekrana yazar:

```text
Watchdog PID: 1741
```

Olaylar `watchdog.log` dosyasına yazılır. Logu canlı izlemek için ikinci bir terminalde:

```bash
tail -f watchdog.log
```

### Başlatılan Worker'lar

Watchdog, tüm senaryoları gösterebilmek için dört test worker'ı başlatır:

| Worker | Mod | Davranış |
|---|---|---|
| worker-1 | `forever` | Sürekli çalışır |
| worker-2 | `normal 3` | 3 saniye sonra `exit(0)` ile normal biter; yeniden başlatılmaz |
| worker-3 | `crash 2` | 2 saniye sonra `SIGABRT` ile çöker; 3 kez yeniden başlatılır, sonra bırakılır |
| worker-4 | `stubborn` | `SIGTERM`'i yok sayar; kapanışta zaman aşımından sonra `SIGKILL` ile sonlandırılır |

## Sinyallerle Kullanım

Watchdog'un PID'ini bulmak için (ya da başlangıçta ekrana yazılan PID'i kullanın):

```bash
pgrep -x watchdog
```

| Komut | Etki |
|---|---|
| `kill -SIGUSR1 <WATCHDOG_PID>` | Worker'ların PID ve durumlarını log dosyasına yazar |
| `kill -SIGTERM <WATCHDOG_PID>` | Kontrollü kapanışı başlatır |
| Ctrl+C (watchdog'un çalıştığı terminalde) | Kontrollü kapanışı başlatır |
| `kill -SIGKILL <WORKER_PID>` | Beklenmedik sonlanma testi: worker yeniden başlatılır |

> Beklenmedik sonlanma testinde yalnızca PID'i doğrulanmış bir test worker'ına sinyal gönderin. Worker PID'lerini `pgrep -x worker`, `SIGUSR1` raporu veya logdaki `START` satırlarıyla görebilirsiniz.

> Terminalde Ctrl+C, `SIGINT`'i aynı ön plan süreç grubundaki worker'lara da gönderir. Bu nedenle Ctrl+C sonrası worker'lar logda `sig=2` ile sonlanmış görünebilir; watchdog kapanış sırasında bunları yeniden başlatmaz ve tümünü toplar. Ayrıntı için `docs/TEST_RESULTS.md` dosyasına bakın.

## Log Biçimi

Her satır zaman damgasıyla başlar:

```text
[YYYY-MM-DD HH:MM:SS] OLAY ayrintilar
```

| Olay | Anlamı |
|---|---|
| `WATCHDOG START` | Watchdog başladı |
| `START` | Bir worker başlatıldı |
| `EXIT ... reason=NORMAL/ERROR/SIGNAL` | Bir worker sonlandı ve toplandı |
| `RESTART <ad> (n/3)` | Beklenmedik sonlanan worker yeniden başlatıldı |
| `GIVE_UP` | Yeniden başlatma sınırı aşıldı |
| `TIMER`, `PERIODIC_CHECK` | Periyodik kontrol zamanlayıcısı ve kontrol sonucu |
| `STATUS` | `SIGUSR1` ile istenen durum raporu |
| `SHUTDOWN`, `SIGNAL` | Kontrollü kapanış adımları ve worker'lara gönderilen sinyaller |

Örnek bir çalıştırmadan alıntı:

```text
[2026-11-24 15:34:06] START worker-1 pid=1744
[2026-11-24 15:34:06] WATCHDOG START pid=1741
[2026-11-24 15:34:08] EXIT worker-3 pid=1746 reason=SIGNAL sig=6
[2026-11-24 15:34:08] RESTART worker-3 (1/3)
[2026-11-24 15:34:09] EXIT worker-2 pid=1745 reason=NORMAL code=0
[2026-11-24 15:34:14] GIVE_UP worker-3 yeniden baslatma siniri asildi (3/3)
[2026-11-24 15:34:17] STATUS worker-1 pid=1744 state=RUNNING restarts=0/3
[2026-11-24 15:34:18] SHUTDOWN basladi: zaman asimi=5 saniye
[2026-11-24 15:34:23] SHUTDOWN TIMEOUT: kalan 1 worker'a SIGKILL
[2026-11-24 15:34:23] SHUTDOWN tamamlandi: tum worker'lar toplandi
```

Tam örnek log: `docs/sample.log`

## Worker Programını Tek Başına Çalıştırma

Test worker'ı watchdog olmadan da çalıştırılabilir:

```bash
./worker <worker_adi> [mod] [sure_sn]
```

| Mod | Davranış |
|---|---|
| `forever` | Sinyal gelene kadar çalışır (varsayılan) |
| `normal` | Belirtilen süre sonunda `exit(0)` ile çıkar (varsayılan 3 sn) |
| `error` | Belirtilen süre sonunda `exit(1)` ile çıkar (varsayılan 2 sn) |
| `crash` | Belirtilen süre sonunda `abort()` ile `SIGABRT` üretir (varsayılan 2 sn) |
| `stubborn` | `SIGTERM`'i yok sayar, yalnızca `SIGKILL` ile sonlanır |

## Testler

```bash
./tests/test_scenarios.sh
```

Script, PDF'teki test senaryolarını otomatik olarak çalıştırır ve sonucu `PASS` / `FAIL` olarak raporlar (yaklaşık 1,5 dakika sürer). Son çalıştırmanın sonucu: **17 PASS, 0 FAIL, 0 SKIP**.

Senaryo bazında sonuçlar, log kanıtları ve elle yapılan Ctrl+C testi: `docs/TEST_RESULTS.md`

## Ayarlar

Sınırlar `include/process_manager.h` içinde tanımlıdır:

| Sabit | Değer | Açıklama |
|---|---|---|
| `PM_MAX_RESTARTS` | 3 | Worker başına en fazla yeniden başlatma |
| `PM_DEFAULT_CHECK_INTERVAL_SEC` | 5 | Periyodik kontrol aralığı (saniye) |
| `PM_SHUTDOWN_TIMEOUT_SEC` | 5 | `SIGKILL` öncesi kapanış bekleme süresi (saniye) |
| `PM_MAX_WORKERS` | 8 | En fazla worker sayısı |

## Proje Yapısı

```text
SysProg1/
├── Makefile
├── README.md
├── include/
│   ├── logger.h            # Loglama arayüzü
│   ├── process_manager.h   # Worker yönetimi arayüzü ve sabitler
│   └── signal_manager.h    # Sinyal bayrakları ve işleyici kurulumu
├── src/
│   ├── main.c              # Ana döngü ve kontrollü kapanış
│   ├── logger.c
│   ├── process_manager.c
│   └── signal_manager.c
├── workers/
│   └── worker.c            # Test amaçlı worker programı
├── tests/
│   └── test_scenarios.sh   # Otomatik test senaryoları
└── docs/
    ├── TEST_RESULTS.md     # Test sonuçları
    ├── sample.log          # Örnek log çıktısı
    ├── burak-module.md     # Sinyal yönetimi ve ana program
    ├── ahmet-module.md     # Alt süreç yönetimi
    ├── utku-module.md      # Loglama ve test altyapısı
    └── REPORT.pdf          # Proje raporu
```

## Ekip ve Görev Dağılımı

| Üye | GitHub | Sorumluluk | Ayrıntı |
|---|---|---|---|
| Muhammet Burak Özyurt | [@Burak-Ozyurt](https://github.com/Burak-Ozyurt) | Sinyal yönetimi, ana döngü, kontrollü kapanış | `docs/burak-module.md` |
| Ahmet Öztürk | [@Ahmetozturk0](https://github.com/Ahmetozturk0) | Alt süreç yönetimi, yeniden başlatma, periyodik kontrol | `docs/ahmet-module.md` |
| Oğulcan Utku Çal | [@UtkuCal](https://github.com/UtkuCal) | Loglama, durum raporu, Makefile, testler, dokümantasyon | `docs/utku-module.md` |
