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

# Watchdog'u temiz bir log dosyasiyla arka planda baslatir.
start_watchdog() {
    rm -f "$LOG"
    ./watchdog >/dev/null 2>&1 &
    WD_PID=$!
    sleep 1
}

# Watchdog hala calisiyorsa SIGTERM ile kapatir ve bitmesini bekler.
stop_watchdog() {
    if [ -n "$WD_PID" ] && kill -0 "$WD_PID" 2>/dev/null; then
        kill -TERM "$WD_PID"
        wait "$WD_PID" 2>/dev/null
    fi
    WD_PID=""
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

# --- Watchdog senaryolari (ana dongu tamamlaninca etkinlesecek) ----------

test_normal_operation() {
    skip "Normal calisma: en az iki worker baslatilir (ana dongu bekleniyor)"
}

test_normal_exit() {
    skip "Normal tamamlanma: exit(0) algilanir, yeniden baslatilmaz"
}

test_unexpected_exit() {
    skip "Beklenmedik sonlanma: SIGKILL sonrasi yeniden baslatma"
}

test_restart_limit() {
    skip "Yeniden baslatma siniri: 3 denemeden sonra durur"
}

test_status_report() {
    skip "Durum raporu: SIGUSR1 sonrasi PID ve durumlar loga yazilir"
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
