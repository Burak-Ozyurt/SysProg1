# Test Sonuçları

Bu belge, watchdog projesinin PDF'te (Bölüm 7) istenen test senaryolarına göre doğrulanmasını içerir. Testlerin çoğu `tests/test_scenarios.sh` ile otomatik çalıştırılır; Ctrl+C testi ayrıca terminalde elle yapılmıştır.

## Test Ortamı

| Bileşen | Sürüm |
|---|---|
| İşletim sistemi | Ubuntu 26.04.1 LTS (Windows üzerinde WSL 2) |
| Çekirdek | Linux 6.18.58.1-microsoft-standard-WSL2 |
| Derleyici | gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0 |
| Derleme bayrakları | `-Wall -Wextra -std=c11 -D_XOPEN_SOURCE=700` (uyarısız derleniyor) |
| Test tarihi | 24 Kasım 2026 |

## Testleri Çalıştırma

Depo kök dizininde:

```bash
make
./tests/test_scenarios.sh
```

Script her senaryo için watchdog'u temiz bir log dosyasıyla başlatır, gerekli sinyalleri gönderir, `watchdog.log` içinde beklenen satırları arar ve sonucu `PASS` / `FAIL` olarak yazar. Tüm senaryolar yaklaşık 1,5 dakika sürer.

## Test Yapılandırması

`src/main.c` her senaryoyu gösterebilmek için dört test worker'ı başlatır:

| Worker | Mod | Amaç |
|---|---|---|
| worker-1 | `forever` | Sürekli çalışır; dışarıdan `SIGKILL` ve kontrollü kapanış testleri için |
| worker-2 | `normal 3` | 3 saniye sonra `exit(0)` ile normal tamamlanır |
| worker-3 | `crash 2` | 2 saniye sonra `SIGABRT` ile çöker; yeniden başlatma sınırı için |
| worker-4 | `stubborn` | `SIGTERM`'i yok sayar; kapanış zaman aşımı ve `SIGKILL` için |

## Otomatik Test Sonuçları

```text
[PASS] Derleme: make watchdog ve worker uretiyor
[PASS] Worker normal modu: 0 koduyla cikiyor
[PASS] Worker error modu: 1 koduyla cikiyor
[PASS] Worker crash modu: SIGABRT ile sonlaniyor
[PASS] Worker forever modu: SIGTERM ile sonlaniyor
[PASS] Worker stubborn modu: SIGTERM'i yoksayiyor, SIGKILL ile sonlaniyor
[PASS] Normal calisma: 4 worker baslatildi ve calisiyor
[PASS] Normal tamamlanma: worker-2 NORMAL olarak algilandi, yeniden baslatilmadi
[PASS] Beklenmedik sonlanma: worker-3 cokmesi (SIGABRT) algilandi
[PASS] Beklenmedik sonlanma: worker-1'e gonderilen SIGKILL algilandi
[PASS] Beklenmedik sonlanma: worker-1 yeniden baslatildi (1/3)
[PASS] Yeniden baslatma siniri: 3 yeniden baslatmadan sonra durdu (toplam 4 baslatma)
[PASS] Durum raporu: SIGUSR1 sonrasi worker PID ve durumlari loga yazildi
[PASS] Kontrollu kapanis (SIGTERM): worker'lara SIGTERM gonderildi, toplandi, watchdog 0 ile cikti
[PASS] Kontrollu kapanis (SIGINT / Ctrl+C): worker'lara SIGTERM gonderildi, toplandi, watchdog 0 ile cikti
[PASS] Kapanis zaman asimi: worker-4 5 sn sonra SIGKILL ile sonlandirildi
[PASS] Zombie kontrolu: calisma sirasinda ve kapanis sonrasi zombie surec yok

Sonuc: 17 PASS, 0 FAIL, 0 SKIP
```

## Senaryolara Göre Sonuçlar

Log kanıtları `docs/sample.log` dosyasından alınmıştır.

### 1. Normal çalışma

| | |
|---|---|
| **Yöntem** | `./watchdog` başlatılır; logdaki `START` satırları ve worker PID'lerinin çalışıp çalışmadığı kontrol edilir. |
| **Beklenen** | En az iki worker `fork()` + `exec()` ile başlatılır ve PID'leri takip edilir. |
| **Gözlenen** | 4 worker başlatıldı, hepsi çalışıyor. ✅ |

```text
[2026-11-24 15:34:06] START worker-1 pid=1744
[2026-11-24 15:34:06] START worker-2 pid=1745
[2026-11-24 15:34:06] START worker-3 pid=1746
[2026-11-24 15:34:06] START worker-4 pid=1747
[2026-11-24 15:34:06] TIMER baslatildi aralik=5 sn
[2026-11-24 15:34:06] WATCHDOG START pid=1741
```

### 2. Normal tamamlanma

| | |
|---|---|
| **Yöntem** | worker-2'nin 3 saniye sonra `exit(0)` ile bitmesi beklenir. |
| **Beklenen** | Çıkış `NORMAL` olarak sınıflandırılır, worker yeniden başlatılmaz. |
| **Gözlenen** | `reason=NORMAL code=0` yazıldı, `RESTART worker-2` satırı yok. ✅ |

```text
[2026-11-24 15:34:09] EXIT worker-2 pid=1745 reason=NORMAL code=0
```

### 3. Beklenmedik sonlanma

| | |
|---|---|
| **Yöntem** | (a) worker-3'ün kendiliğinden `SIGABRT` ile çökmesi beklenir. (b) worker-1'in PID'i logdan alınır, `ps` ile gerçekten bir `worker` süreci olduğu doğrulanır ve `kill -SIGKILL <PID>` gönderilir. |
| **Beklenen** | Sinyalle sonlanma `SIGNAL` olarak sınıflandırılır ve worker yeniden başlatılır. |
| **Gözlenen** | worker-3 için `sig=6`, worker-1 için `sig=9` algılandı; ikisi de yeniden başlatıldı. ✅ |

```text
[2026-11-24 15:34:08] EXIT worker-3 pid=1746 reason=SIGNAL sig=6
[2026-11-24 15:34:08] RESTART worker-3 (1/3)
[2026-11-24 15:34:08] START worker-3 pid=1757
```

### 4. Yeniden başlatma sınırı

| | |
|---|---|
| **Yöntem** | worker-3 her 2 saniyede bir çöker; `GIVE_UP` satırından sonra 3 saniye daha beklenip yeni deneme yapılıp yapılmadığına bakılır. |
| **Beklenen** | İlk başlatmadan sonra en fazla 3 yeniden başlatma; ardından başka deneme yapılmaz. |
| **Gözlenen** | `(1/3)`, `(2/3)`, `(3/3)` sonrası `GIVE_UP`; toplam 4 başlatma, 4. yeniden başlatma yok. ✅ |

```text
[2026-11-24 15:34:10] RESTART worker-3 (2/3)
[2026-11-24 15:34:12] RESTART worker-3 (3/3)
[2026-11-24 15:34:14] EXIT worker-3 pid=1759 reason=SIGNAL sig=6
[2026-11-24 15:34:14] GIVE_UP worker-3 yeniden baslatma siniri asildi (3/3)
```

### 5. Durum raporu (SIGUSR1)

| | |
|---|---|
| **Yöntem** | `kill -SIGUSR1 <WATCHDOG_PID>` |
| **Beklenen** | Her worker'ın PID'i ve güncel durumu log dosyasına yazılır. |
| **Gözlenen** | Dört worker'ın durumu (RUNNING, COMPLETED, FAILED) ve yeniden başlatma sayıları yazıldı. ✅ |

```text
[2026-11-24 15:34:17] STATUS rapor: toplam=4 calisan=2
[2026-11-24 15:34:17] STATUS worker-1 pid=1744 state=RUNNING restarts=0/3
[2026-11-24 15:34:17] STATUS worker-2 pid=- state=COMPLETED restarts=0/3 last_exit=NORMAL
[2026-11-24 15:34:17] STATUS worker-3 pid=- state=FAILED restarts=3/3 last_exit=SIGNAL
[2026-11-24 15:34:17] STATUS worker-4 pid=1747 state=RUNNING restarts=0/3
```

### 6. Kontrollü kapanış (SIGTERM)

| | |
|---|---|
| **Yöntem** | `kill -SIGTERM <WATCHDOG_PID>` |
| **Beklenen** | Yeni süreç başlatma durur, çalışan worker'lara `SIGTERM` gönderilir, worker'lar toplanır, watchdog 0 koduyla çıkar. |
| **Gözlenen** | worker-1 `SIGTERM` ile kapandı; worker-4 (stubborn) 5 saniyelik süre sonunda `SIGKILL` ile sonlandırıldı. Tüm worker'lar toplandı, çıkış kodu 0. ✅ |

```text
[2026-11-24 15:34:18] SHUTDOWN basladi: zaman asimi=5 saniye
[2026-11-24 15:34:18] SIGNAL SIGTERM worker-1 pid=1744
[2026-11-24 15:34:18] SIGNAL SIGTERM worker-4 pid=1747
[2026-11-24 15:34:18] EXIT worker-1 pid=1744 reason=SIGNAL sig=15
[2026-11-24 15:34:23] SHUTDOWN TIMEOUT: kalan 1 worker'a SIGKILL
[2026-11-24 15:34:23] SIGNAL SIGKILL worker-4 pid=1747
[2026-11-24 15:34:23] EXIT worker-4 pid=1747 reason=SIGNAL sig=9
[2026-11-24 15:34:23] SHUTDOWN tamamlandi: tum worker'lar toplandi
```

### 7. Kontrollü kapanış (Ctrl+C) — elle yapılan test

| | |
|---|---|
| **Yöntem** | `./watchdog` terminalde ön planda çalıştırıldı; birkaç saniye sonra Ctrl+C'ye basıldı, ardından `echo $?` ve logdaki kapanış satırları incelendi. |
| **Beklenen** | Kontrollü kapanış başlar, worker'lar toplanır, kapanış sırasında yeniden başlatma yapılmaz, watchdog 0 koduyla çıkar. |
| **Gözlenen** | Kapanış başladı, worker'lar toplandı, `RESTART` satırı oluşmadı, çıkış kodu 0. ✅ |

```text
[2026-11-24 15:34:53] SHUTDOWN basladi: zaman asimi=5 saniye
[2026-11-24 15:34:53] SIGNAL SIGTERM worker-1 pid=1814
[2026-11-24 15:34:53] SIGNAL SIGTERM worker-4 pid=1817
[2026-11-24 15:34:53] EXIT worker-1 pid=1814 reason=SIGNAL sig=2
[2026-11-24 15:34:53] EXIT worker-4 pid=1817 reason=SIGNAL sig=2
[2026-11-24 15:34:53] SHUTDOWN tamamlandi: tum worker'lar toplandi
```

**Not:** Terminalde Ctrl+C, `SIGINT`'i yalnızca watchdog'a değil, aynı ön plan süreç grubundaki worker'lara da gönderir. Bu yüzden worker'lar watchdog'un gönderdiği `SIGTERM`'den önce doğrudan terminalden gelen `SIGINT` ile (`sig=2`) sonlandı. Watchdog yine de kapanış akışını eksiksiz uyguladı: kapanış modunda bu sonlanmaları yeniden başlatmadı, tüm worker'ları toplayıp 0 koduyla çıktı. `SIGTERM` gönderme ve zaman aşımı adımları, sinyalin yalnızca watchdog'a gönderildiği 6. senaryoda ve otomatik SIGINT testinde ayrıca doğrulanmıştır.

### 8. Log doğrulama

| | |
|---|---|
| **Yöntem** | Tüm otomatik testler sonucu log satırları üzerinden kontrol eder; ayrıca `docs/sample.log` elle incelenmiştir. |
| **Beklenen** | Başlatma, sonlanma, yeniden başlatma, durum raporu ve kapanış olayları zaman damgasıyla loglanır. |
| **Gözlenen** | Tüm olaylar `[YYYY-MM-DD HH:MM:SS]` zaman damgasıyla, PID ve durum bilgisiyle yazılmıştır. Periyodik kontrol satırları 5 saniye aralıkla oluşmaktadır. ✅ |

```text
[2026-11-24 15:34:11] PERIODIC_CHECK aktif=3 toplam=4
[2026-11-24 15:34:16] PERIODIC_CHECK aktif=2 toplam=4
```

## Ek Kontroller

| Kontrol | Sonuç |
|---|---|
| Kapanış zaman aşımı: `SIGTERM`'i yok sayan worker'a süre sonunda `SIGKILL` | ✅ 5 saniye sonra `SIGKILL`, worker toplandı |
| Zombie süreç: çalışma sırasında ve kapanıştan sonra `defunct` worker | ✅ Bulunmadı |
| Kapanış sırasında yeniden başlatma ve periyodik kontrol | ✅ Kapanış başladıktan sonra `RESTART` veya `PERIODIC_CHECK` satırı oluşmadı |
| Derleyici uyarıları | ✅ `-Wall -Wextra` ile uyarısız derleniyor |

## Özet

PDF Bölüm 7'deki yedi test senaryosunun tamamı başarıyla doğrulanmıştır. Otomatik test scripti 17 kontrolün tamamında `PASS` vermiş, Ctrl+C testi elle yapılarak sonucu yukarıda belgelenmiştir.
