# Lab-1 Minggu 2 — Pengendali Greenhouse Kopi (ESP32)

GPIO digital/analog, ADC1 terkalibrasi, moving average, histeresis relai, PWM LEDC, DHT22, dan pengaman dry-run pompa (C6).

## Struktur
```
firmware/
  platformio.ini     konfigurasi PlatformIO + pustaka
  diagram.json       skema Wokwi (ditambah LED merah fault di GPIO 27)
  wokwi.toml         jalur firmware untuk Wokwi
  src/main.cpp       program utama (termasuk C6)
docs/
  laporan-lab01-minggu-02.md   laporan + jawaban C1–C6 + Tabel 3.1–3.2
  bukti-uji-simulasi-host.txt  log uji main.cpp di PC
sim/                 stub Arduino + harness uji host (tanpa perangkat keras)
```

## Menjalankan
- **PlatformIO:** `cd firmware && pio run` lalu buka Wokwi (`wokwi.toml` sudah mengarah ke `.pio/build/esp32dev/`).
- **Uji host (opsional):** `bash sim/run_tests.sh` (perlu `g++`) memperbarui `docs/bukti-uji-simulasi-host.txt`.

## Opsi kompilasi
| Makro | Default | Fungsi |
|---|---|---|
| `GUNAKAN_HISTERESIS` | 1 | 0 = ambang tunggal 45% (menampilkan chattering) |
| `PENGAMAN_DRY_RUN` | 1 | 0 hanya untuk Eksperimen 2 agar hitungan klik relai murni |

## Git (sesuai alokasi waktu praktikum)
```bash
git checkout -b feat/lab01-sensor-actuator
git add firmware docs sim README.md
git commit -m "feat(lab01): ADC1 terkalibrasi, histeresis, PWM, dry-run protection (C6)"
git push -u origin feat/lab01-sensor-actuator
```

## Catatan keselamatan
Relai pompa 220 V AC: jangan menyambung beban AC tanpa pengawasan asisten.
