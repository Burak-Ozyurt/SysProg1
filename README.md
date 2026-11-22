# Watchdog: POSIX Sinyal Tabanlı Süreç İzleme ve Yönetim Sistemi

Sakarya Üniversitesi Yazılım Mühendisliği, Sistem Programlama 2026–2027 Güz Dönemi Proje 1.

`watchdog`, C dilinde ve POSIX sistem çağrılarıyla yazılmış bir süreç denetleyicisidir. Başlattığı alt süreçleri (worker) izler, beklenmedik şekilde sonlananları sınırlı sayıda yeniden başlatır ve kullanıcının gönderdiği sinyallere göre durum raporu üretir veya kontrollü şekilde kapanır.

## Özellikler

- `fork()` ve `exec()` ile en az iki worker başlatma
- `SIGCHLD` ve `waitpid()` ile sonlanan worker'ları tespit etme ve toplama
- Normal çıkış, hata koduyla çıkış ve sinyalle sonlanmayı ayırt etme
- Beklenmedik şekilde sonlanan worker'ı en fazla **3 kez** yeniden başlatma (ilk başlatma sayılmaz)
- `SIGUSR1` ile worker'ların PID ve durumlarını log dosyasına yazma
- `SIGINT` (Ctrl+C) ve `SIGTERM` ile kontrollü kapanış: worker'lara `SIGTERM`, zaman aşımında `SIGKILL`
- `setitimer()` / `SIGALRM` ile periyodik kontrol
- Önemli olayların zaman damgasıyla `watchdog.log` dosyasına yazılması

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

Bu komut `watchdog` ve `worker` çalıştırılabilir dosyalarını üretir. Derleme çıktılarını silmek için:

```bash
make clean
```

## Çalıştırma

Program depo kök dizininden başlatılmalıdır (worker `./worker` yolundan çalıştırılır):

```bash
./watchdog
```

Olaylar `watchdog.log` dosyasına yazılır. Logu canlı izlemek için ikinci bir terminalde:

```bash
tail -f watchdog.log
```

## Sinyallerle Kullanım

Watchdog'un PID'ini bulmak için:

```bash
pgrep -x watchdog
```

| Komut | Etki |
|---|---|
| `kill -SIGUSR1 <WATCHDOG_PID>` | Worker'ların PID ve durumlarını log dosyasına yazar |
| `kill -SIGTERM <WATCHDOG_PID>` | Kontrollü kapanışı başlatır |
| Ctrl+C (watchdog'un çalıştığı terminalde) | Kontrollü kapanışı başlatır |
| `kill -SIGKILL <WORKER_PID>` | Beklenmedik sonlanma testi: worker yeniden başlatılır |

> Beklenmedik sonlanma testinde yalnızca PID'i doğrulanmış bir test worker'ına sinyal gönderin. Worker PID'lerini `pgrep -x worker` veya `SIGUSR1` raporuyla görebilirsiniz.

## Log Biçimi

Her satır zaman damgasıyla başlar:

```text
[YYYY-MM-DD HH:MM:SS] OLAY ayrıntılar
```

## Testler

```bash
./tests/test_scenarios.sh
```

Test senaryoları ve sonuçları `docs/TEST_RESULTS.md` dosyasında yer alır.

## Proje Yapısı

```text
SysProg1/
├── Makefile
├── README.md
├── include/
│   ├── logger.h            # Loglama arayüzü
│   ├── process_manager.h   # Worker yönetimi arayüzü
│   └── signal_manager.h    # Sinyal bayrakları ve işleyici kurulumu
├── src/
│   ├── main.c              # Ana döngü ve kontrollü kapanış
│   ├── logger.c
│   ├── process_manager.c
│   └── signal_manager.c
├── workers/
│   └── worker.c            # Test amaçlı worker programı
├── tests/
│   └── test_scenarios.sh
└── docs/
```

## Ekip ve Görev Dağılımı

| Üye | GitHub | Sorumluluk |
|---|---|---|
| Muhammet Burak Özyurt | [@Burak-Ozyurt](https://github.com/Burak-Ozyurt) | Sinyal yönetimi, ana döngü, kontrollü kapanış |
| Ahmet Öztürk | [@Ahmetozturk0](https://github.com/Ahmetozturk0) | Alt süreç yönetimi, yeniden başlatma, periyodik kontrol |
| Oğulcan Utku Çal | [@UtkuCal](https://github.com/UtkuCal) | Loglama, durum raporu, Makefile, testler, dokümantasyon |
