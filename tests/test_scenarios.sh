#!/usr/bin/env bash
# Watchdog test senaryolari
# Kullanim (depo kok dizininden): ./tests/test_scenarios.sh

set -u
cd "$(dirname "$0")/.." || exit 1

LOG=watchdog.log
PASS=0
FAIL=0
SKIP=0
WD_PID=""

pass() { echo "[PASS] $1"; PASS=$((PASS + 1)); }
fail() { echo "[FAIL] $1"; FAIL=$((FAIL + 1)); }
skip() { echo "[SKIP] $1"; SKIP=$((SKIP + 1)); }

# Watchdog'u temiz bir log dosyasiyla arka planda baslatir ve
# "WATCHDOG START" satiri gorunene kadar (en fazla 3 sn) bekler.
start_watchdog() {
    rm -f "$LOG"
    ./watchdog >/dev/null 2>&1 &
    WD_PID=$!
    wait_for_log "WATCHDOG START" 3
}

# Watchdog'a SIGTERM gonderir ve en fazla 3 sn kapanmasini bekler.
# Kapanmazsa SIGKILL ile durdurur; geride kalan worker'lari da temizler
# ki bir test digerini etkilemesin.
stop_watchdog() {
    local i
    if [ -n "$WD_PID" ] && kill -0 "$WD_PID" 2>/dev/null; then
        kill -TERM "$WD_PID"
        for ((i = 0; i < 30; i++)); do
            kill -0 "$WD_PID" 2>/dev/null || break
            sleep 0.1
        done
        kill -0 "$WD_PID" 2>/dev/null && kill -KILL "$WD_PID"
    fi
    [ -n "$WD_PID" ] && wait "$WD_PID" 2>/dev/null
    pkill -x worker 2>/dev/null
    WD_PID=""
}

# Logdaki son "START <ad> pid=N" satirindan worker'in PID'ini verir.
worker_pid() {
    sed -n "s/.*START $1 pid=\([0-9]*\).*/\1/p" "$LOG" | tail -n 1
}

# Log dosyasinda verilen ifade gecene kadar en fazla $2 saniye bekler.
wait_for_log() {
    local pattern=$1 timeout=${2:-5} i
    for ((i = 0; i < timeout * 10; i++)); do
        grep -q -- "$pattern" "$LOG" 2>/dev/null && return 0
        sleep 0.1
    done
    return 1
}

# --- Derleme ------------------------------------------------------------

test_build() {
    if make >/dev/null 2>&1 && [ -x ./watchdog ] && [ -x ./worker ]; then
        pass "Derleme: make watchdog ve worker uretiyor"
    else
        fail "Derleme: make basarisiz veya watchdog/worker yok"
    fi
}

# --- Worker test modlari ------------------------------------------------
# Watchdog'un ayirt etmesi gereken uc cikis turunu worker'in gercekten
# urettigini dogrular: normal (0), hata kodu (1) ve sinyalle sonlanma.

test_worker_normal() {
    ./worker w-normal normal 1 >/dev/null 2>&1
    local rc=$?
    if [ "$rc" -eq 0 ]; then
        pass "Worker normal modu: 0 koduyla cikiyor"
    else
        fail "Worker normal modu: beklenen 0, gelen $rc"
    fi
}

test_worker_error() {
    ./worker w-error error 1 >/dev/null 2>&1
    local rc=$?
    if [ "$rc" -eq 1 ]; then
        pass "Worker error modu: 1 koduyla cikiyor"
    else
        fail "Worker error modu: beklenen 1, gelen $rc"
    fi
}

test_worker_crash() {
    # Bash, sinyalle sonlanan surec icin 128 + sinyal numarasi dondurur.
    # SIGABRT = 6 oldugu icin beklenen deger 134'tur. Arka planda baslatip
    # wait'in hata cikisini gizlemek, bash'in yazdigi "Aborted" mesajini onler.
    ./worker w-crash crash 1 >/dev/null 2>&1 &
    wait $! 2>/dev/null
    local rc=$?
    if [ "$rc" -eq 134 ]; then
        pass "Worker crash modu: SIGABRT ile sonlaniyor"
    else
        fail "Worker crash modu: beklenen 134 (SIGABRT), gelen $rc"
    fi
}

test_worker_forever_sigterm() {
    ./worker w-forever forever >/dev/null 2>&1 &
    local pid=$!
    sleep 0.5
    kill -TERM "$pid"
    wait "$pid" 2>/dev/null
    local rc=$?
    # SIGTERM = 15 -> 128 + 15 = 143
    if [ "$rc" -eq 143 ]; then
        pass "Worker forever modu: SIGTERM ile sonlaniyor"
    else
        fail "Worker forever modu: beklenen 143 (SIGTERM), gelen $rc"
    fi
}

test_worker_stubborn() {
    ./worker w-stubborn stubborn >/dev/null 2>&1 &
    local pid=$!
    sleep 0.5
    kill -TERM "$pid"
    sleep 1
    if kill -0 "$pid" 2>/dev/null; then
        kill -KILL "$pid"
        wait "$pid" 2>/dev/null
        pass "Worker stubborn modu: SIGTERM'i yoksayiyor, SIGKILL ile sonlaniyor"
    else
        wait "$pid" 2>/dev/null
        fail "Worker stubborn modu: SIGTERM ile kapandi (yoksaymaliydi)"
    fi
}

# --- Watchdog senaryolari ------------------------------------------------

test_normal_operation() {
    start_watchdog
    local starts pid1 pid2
    starts=$(grep -c "\] START worker-" "$LOG" 2>/dev/null)
    pid1=$(worker_pid worker-1)
    pid2=$(worker_pid worker-2)
    if [ "${starts:-0}" -ge 2 ] && [ -n "$pid1" ] && [ -n "$pid2" ] &&
       kill -0 "$pid1" 2>/dev/null && kill -0 "$pid2" 2>/dev/null; then
        pass "Normal calisma: $starts worker baslatildi ve calisiyor"
    else
        fail "Normal calisma: en az iki calisan worker bulunamadi (START sayisi: ${starts:-0})"
    fi
    stop_watchdog
}

test_normal_exit() {
    start_watchdog
    if wait_for_log "EXIT worker-2 .*reason=NORMAL" 6; then
        sleep 1
        if grep -q "RESTART worker-2" "$LOG"; then
            fail "Normal tamamlanma: worker-2 normal bitti ama yeniden baslatildi"
        else
            pass "Normal tamamlanma: worker-2 NORMAL olarak algilandi, yeniden baslatilmadi"
        fi
    else
        fail "Normal tamamlanma: 'EXIT worker-2 ... reason=NORMAL' satiri bulunamadi"
    fi
    stop_watchdog
}

test_unexpected_exit() {
    start_watchdog
    local pid comm

    # 1) worker-3 kendiliginden SIGABRT ile coker.
    if wait_for_log "EXIT worker-3 .*reason=SIGNAL sig=6" 5; then
        pass "Beklenmedik sonlanma: worker-3 cokmesi (SIGABRT) algilandi"
    else
        fail "Beklenmedik sonlanma: worker-3 icin 'reason=SIGNAL sig=6' bulunamadi"
    fi

    # 2) worker-1'e disaridan SIGKILL gonderilir. PDF geregi once PID'in
    #    gercekten bir test worker'ina ait oldugu dogrulanir.
    pid=$(worker_pid worker-1)
    comm=$(ps -p "${pid:-0}" -o comm= 2>/dev/null)
    if [ -z "$pid" ] || [ "$comm" != "worker" ]; then
        fail "Beklenmedik sonlanma: worker-1 PID'i dogrulanamadi"
    else
        kill -KILL "$pid"
        if wait_for_log "EXIT worker-1 pid=$pid reason=SIGNAL sig=9" 3; then
            pass "Beklenmedik sonlanma: worker-1'e gonderilen SIGKILL algilandi"
        else
            fail "Beklenmedik sonlanma: SIGKILL sonrasi 'reason=SIGNAL sig=9' bulunamadi"
        fi
        if wait_for_log "RESTART worker-1 (1/3)" 3; then
            pass "Beklenmedik sonlanma: worker-1 yeniden baslatildi (1/3)"
        else
            fail "Beklenmedik sonlanma: worker-1 icin 'RESTART worker-1 (1/3)' bulunamadi"
        fi
    fi
    stop_watchdog
}

test_restart_limit() {
    start_watchdog
    local starts

    # worker-3 her 2 sn'de coker: 1 ilk baslatma + 3 yeniden baslatma,
    # ardindan GIVE_UP. Toplam yaklasik 8 sn surer.
    if ! wait_for_log "GIVE_UP worker-3" 12; then
        fail "Yeniden baslatma siniri: 'GIVE_UP worker-3' satiri bulunamadi"
        stop_watchdog
        return
    fi

    # GIVE_UP'tan sonra yeni bir deneme yapilmadigini gormek icin biraz bekle.
    sleep 3
    starts=$(grep -c "\] START worker-3 " "$LOG")

    if grep -q "RESTART worker-3 (1/3)" "$LOG" &&
       grep -q "RESTART worker-3 (2/3)" "$LOG" &&
       grep -q "RESTART worker-3 (3/3)" "$LOG" &&
       ! grep -q "RESTART worker-3 (4/3)" "$LOG" &&
       [ "$starts" -eq 4 ]; then
        pass "Yeniden baslatma siniri: 3 yeniden baslatmadan sonra durdu (toplam $starts baslatma)"
    else
        fail "Yeniden baslatma siniri: beklenen 3 RESTART ve 4 START, gelen START sayisi: $starts"
    fi
    stop_watchdog
}

test_status_report() {
    start_watchdog
    local pid1
    pid1=$(worker_pid worker-1)

    kill -USR1 "$WD_PID"
    if wait_for_log "STATUS rapor:" 3 &&
       grep -q "STATUS worker-1 pid=$pid1 state=RUNNING" "$LOG"; then
        pass "Durum raporu: SIGUSR1 sonrasi worker PID ve durumlari loga yazildi"
    else
        fail "Durum raporu: SIGUSR1 sonrasi 'STATUS worker-1 pid=$pid1 state=RUNNING' bulunamadi"
    fi
    stop_watchdog
}

test_sigterm_shutdown() {
    skip "Kontrollu kapanis: SIGTERM ile worker'lar sonlandirilip toplanir"
}

test_sigint_shutdown() {
    skip "Ctrl+C (SIGINT) ile kontrollu kapanis"
}

test_shutdown_timeout() {
    skip "Kapanis zaman asimi: SIGTERM'e yanit vermeyen worker'a SIGKILL"
}

test_no_zombies() {
    skip "Zombie kontrolu: kapanis sonrasi defunct surec kalmaz"
}

# --- Calistir -----------------------------------------------------------

trap stop_watchdog EXIT

test_build

test_worker_normal
test_worker_error
test_worker_crash
test_worker_forever_sigterm
test_worker_stubborn

test_normal_operation
test_normal_exit
test_unexpected_exit
test_restart_limit
test_status_report
test_sigterm_shutdown
test_sigint_shutdown
test_shutdown_timeout
test_no_zombies

echo
echo "Sonuc: $PASS PASS, $FAIL FAIL, $SKIP SKIP"
[ "$FAIL" -eq 0 ]
