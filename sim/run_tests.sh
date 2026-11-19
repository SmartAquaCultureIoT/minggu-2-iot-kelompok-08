#!/usr/bin/env bash
# Menjalankan firmware/src/main.cpp ASLI di PC memakai stub Arduino (tanpa perangkat keras).
# Pemakaian:  bash sim/run_tests.sh   ->  menulis docs/bukti-uji-simulasi-host.txt
set -euo pipefail
cd "$(dirname "$0")"
B="g++ -std=c++17 -Wall -Wno-unused -Istub harness.cpp"
$B -DGUNAKAN_HISTERESIS=1 -o /tmp/h1  ; $B -DGUNAKAN_HISTERESIS=0 -o /tmp/h0
$B -DGUNAKAN_HISTERESIS=1 -DPENGAMAN_DRY_RUN=0 -o /tmp/h1n ; $B -DGUNAKAN_HISTERESIS=0 -DPENGAMAN_DRY_RUN=0 -o /tmp/h0n
stat() { for s in $(seq 1 20); do "$1" wobble "$2" 1 rnd "$s"; done | awk '{s+=$1; if(NR==1||$1<mn)mn=$1; if(NR==1||$1>mx)mx=$1} END{printf "min=%d rata-rata=%.1f maks=%d",mn,s/NR,mx}'; }
OUT=../docs/bukti-uji-simulasi-host.txt
{
echo "BUKTI UJI SIMULASI HOST - Lab-1 Minggu 2 (firmware/src/main.cpp dijalankan apa adanya dengan stub Arduino)"
echo "Catatan: ADC dimodelkan ideal (adc_raw = mV x 4095 / 3300). Nilai perangkat keras/Wokwi bisa sedikit berbeda."
echo; echo "=== [E1] Tabel 3.1 (histeresis, pompa dari sisi kering -> basah) ==="; /tmp/h1 t31
echo; echo "=== [E3] PWM kipas vs suhu (duty 0-255) ==="; /tmp/h1 pwm
echo; echo "=== [E2] Chattering vs histeresis, 30 detik, pengaman C6 dimatikan (PENGAMAN_DRY_RUN=0) ==="
echo "Ambang tunggal 45%, goyang 44-46% bergantian tiap detik : $(/tmp/h0n wobble 45 1 alt 1) transisi relai"
echo "Ambang tunggal 45%, derau acak +-1% (20 seed)           : $(stat /tmp/h0n 45)"
echo "Histeresis 35-60%, goyang 44-46% bergantian              : $(/tmp/h1n wobble 45 1 alt 1) transisi relai"
echo "Histeresis 35-60%, derau acak 44-46% (20 seed)           : $(stat /tmp/h1n 45)"
echo "Histeresis 35-60%, goyang 34-36% bergantian              : $(/tmp/h1n wobble 35 1 alt 1) transisi relai"
echo "Histeresis 35-60%, derau acak 34-36% (20 seed)           : $(stat /tmp/h1n 35)"
echo; echo "=== [C6] Dry-run: tandon habis, soil_pct tetap 20% (pengaman aktif) ==="; /tmp/h1 c6
echo; echo "=== [C6] Soil naik 1%/detik saat pompa jalan -> TIDAK boleh fault (cuplikan akhir) ==="; /tmp/h1 c6rise | tail -1
echo; echo "=== [C6] Override manual + sensor terlepas -> tetap harus fault ==="; /tmp/h1 c6override | grep -E 'event'
echo; echo "=== [C6] DHT22 gagal baca -> pengaman pompa tetap bekerja ==="; /tmp/h1 c6dhtfail | grep -E 'event|relai'
} > "$OUT"
echo "Selesai: $OUT"
