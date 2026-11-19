# Laporan Praktikum Lab-1 — Minggu ke-2

**Mata Kuliah:** Teknologi Internet of Things (IoT), S1 Informatika, Fakultas Teknik, Universitas Bengkulu

**Proyek:** GPIO Digital/Analog, PWM, DHT22 & Soil Moisture

| Nama | NIM | Peran |
|---|---|---|
| Kenia Nurma Feblia | [G1A023004] | Project Manager & Dashboard Engineer |
| Primanda Nafissa Alfiansyah | [G1A023044] | Device & Firmware Engineer |
| Hesi Desta Lestari | [G1A023006] | Back-End & Data Engineer |
| M. Dimas Al-Shiddiq | [G1A023092] | Security & DevOps Engineer |

> **Catatan keterbukaan data.** Semua angka pada Tabel 3.1–3.2 di bawah dihasilkan dengan menjalankan
> `firmware/src/main.cpp` apa adanya pada simulator host (`sim/run_tests.sh`) dengan **model ADC ideal**
> (`adc_raw = mV × 4095 / 3300`). Log lengkapnya ada di `docs/bukti-uji-simulasi-host.txt`.
> Sebelum dikumpulkan, **jalankan ulang di Wokwi/papan fisik**, ganti angka bila berbeda, dan lampirkan
> tangkapan layar pada folder `docs/img/` (daftar bukti di bagian 5).

---

## 1. Analisis Lembar Kerja

**Tujuan.** Membangun pengendali mikro-iklim greenhouse kopi pada ESP32: (1) sensor DHT22 + soil moisture
analog pada pin aman; (2) kalibrasi dua titik terbalik dengan `analogReadMilliVolts()` + moving average;
(3) membuktikan histeresis dua ambang mencegah *chattering* relai; (4) kendali daya kipas dengan PWM LEDC.

**Peta pin dan alasan keamanannya**

| Fungsi | GPIO | Alasan aman |
|---|---|---|
| Soil moisture (analog) | 34 | ADC1_CH6, tidak bentrok dengan Wi-Fi; input-only |
| DHT22 (data) | 4 | I/O digital biasa; dipakai digital sehingga status ADC2 tidak berpengaruh |
| Pushbutton override | 15 | `INPUT_PULLUP`; GPIO 15 pin *strapping* (hanya mengatur log boot), aman selama tombol tidak ditekan saat boot |
| Relai pompa + LED hijau | 26 | Output digital; ADC2 tidak dipakai analog |
| PWM kipas (LED biru) | 18 | Output LEDC, 5 kHz, 8 bit |
| LED fault merah (tambahan C6) | 27 | Pin serbaguna aman, bukan strapping |

**Alur kendali (satu siklus per 1000 ms):** baca ADC → rata-rata geser N=5 → `soil_pct` dari kalibrasi terbalik
`(2650 − mV) × 100 / (2650 − 1100)` dibatasi 0–100 → keputusan pompa (fault > override > histeresis 35%/60% atau
ambang tunggal 45%) → PWM kipas dari suhu (28 °C → 0%, 38 °C → 100%) → telemetri JSON.

**Temuan analisis (hal yang perlu diperhatikan)**

1. `MV_TANAH_KERING = 2650 mV` berada **di atas batas linear 2450 mV** (ADC 11 dB). Ujung kering kurva
   terkompresi sehingga pada sensor fisik nilai ini **harus diukur ulang** (tisu kering/gelas air), bukan memakai
   konstanta bawaan (lihat C2).
2. Pada kode asli, pembacaan DHT22 yang gagal menjalankan `return` **sebelum** kendali pompa selesai. Pengaman
   dry-run tidak boleh ikut mati karena sensor udara bermasalah, sehingga urutan diubah: kendali pompa lebih dulu,
   DHT22 sesudahnya (diuji, lihat bagian 4).
3. Histeresis bergantung pada **riwayat**: titik 50% memberi pompa ON bila dicapai dari sisi kering, OFF bila dari
   sisi basah (Tabel 3.1, baris 3 dan catatan di bawahnya).
4. Rentang PWM memakai `map()` bilangan bulat; hasil dipotong ke bawah (dipakai pada C3).
5. Pada potensiometer, tanah "tidak basah" walau pompa menyala. Karena itu pengaman C6 akan memutus pompa saat
   uji histeresis di sekitar 35%. Untuk Eksperimen 2 gunakan `-DPENGAMAN_DRY_RUN=0` atau ubah
   `#define PENGAMAN_DRY_RUN 0` sementara, lalu kembalikan ke `1` sebelum verifikasi asisten.

---

## 2. Hasil Pengamatan

### Tabel 3.1 — Karakterisasi Kuantisasi ADC1 (GPIO 34) & Pemetaan Kelembapan Tanah

| Uji | Kondisi Input / Potensio | adc_raw (0–4095) | soil_mv_avg (mV) | soil_pct (%) | Status Pompa |
|---|---|---|---|---|---|
| 1 | Ujung Kering (~0%) | 3288 | 2650 | 0,0 | ON |
| 2 | Agak Kering (~30%) | 2711 | 2185 | 30,0 | ON |
| 3 | Lembap Ideal (~50%) | 2327 | 1875 | 50,0 | ON (dari sisi kering) / OFF (dari sisi basah) |
| 4 | Basah Cukup (~70%) | 1942 | 1565 | 70,0 | OFF |
| 5 | Jenuh Air (~100%) | 1365 | 1100 | 100,0 | OFF |

Pembacaan diambil setelah ≥ 5 detik per titik agar buffer moving average terisi penuh. Baris 3 membuktikan
sifat histeresis: pada 35% < `soil_pct` < 60% status pompa bergantung pada kondisi sebelumnya.

### Tabel 3.2 — Perbandingan Ambang Tunggal vs Histeresis (Uji Derau 30 Detik)

Goyangan dimodelkan sebagai perubahan ±1% bergantian tiap detik. Dijalankan juga 20 pengulangan derau acak ±1%.

| Skema Algoritma Kendali | Jumlah Klik Relai (`relay_switches`) | Analisis Dampak terhadap Umur Relai & Beban Jaringan MQTT |
|---|---|---|
| Mode 0: Ambang Tunggal (45%) | **26** (bergantian); acak: rata-rata 6,0 (rentang 2–10) | Relai berpindah hampir tiap detik (≈ 75.000 klik/hari). Kontak AC cepat aus dan jumlah pesan MQTT berbasis kejadian membengkak |
| Mode 1: Histeresis (35%–60%) | **0** di zona 44–46%; **1** di sekitar 35% (menyala sekali lalu bertahan sampai 60%) | Derau ±1% tidak melintasi celah 25 poin, sehingga relai hanya berubah saat kebutuhan air nyata |

---

## 3. Jawaban Pertanyaan Analisis C1–C6

### C1 — Mengingat
GPIO 34 adalah pin **input-only** (GPIO 34–39). Pad-nya tidak memiliki driver keluaran, sehingga tidak dapat
dijadikan output relai, dan tidak memiliki resistor pull-up/pull-down internal, sehingga `INPUT_PULLUP`
tidak berefek. Untuk sensor analog hal ini justru benar: tegangan harus ditentukan sepenuhnya oleh sensor. Pull-up
(seandainya ada) akan membentuk pembagi tegangan dengan impedansi keluaran sensor dan menggeser pembacaan. Karena
itu cukup `pinMode(34, INPUT)`.

### C2 — Memahami
- `analogRead(34)` mengembalikan **kode ADC mentah** 0–4095 (12 bit) yang hanya sebanding dengan tegangan
  masukan secara kasar.
- `analogReadMilliVolts(34)` mengonversi kode itu menjadi **milivolt** dengan data kalibrasi eFuse (Vref/dua titik)
  untuk atenuasi yang dipilih, sehingga mengoreksi gain dan offset tiap chip.
- ADC SAR ESP32 tidak linear pada ujung rentang. Di bawah ≈ 150 mV terdapat zona mati/offset, sedangkan di atas
  ≈ 2450 mV (11 dB) jalur atenuator mengalami kompresi/saturasi. Kode mentah di luar rentang itu tidak
  proporsional terhadap tegangan, dan kalibrasi hanya memperbaiki sebagian. Sebagai dampaknya, konstanta
  `MV_TANAH_KERING = 2650 mV` jatuh di zona terkompresi sehingga perlu diukur ulang pada sensor fisik.

### C3 — Menerapkan
Rumus kode: `duty = map((long)(T×10), 280, 380, 0, 255)` = `(T×10 − 280) × 255 / 100` (pembagian bilangan bulat,
dibulatkan ke bawah), lalu `fan_pwm_pct = duty × 100 / 255` dan `Veff = duty/255 × 3,3 V`.

| Suhu | T×10 | Duty (0–255) | fan_pwm_pct | Veff (3,3 V) |
|---|---|---|---|---|
| 31,5 °C | 315 | (35 × 255)/100 = 8925/100 → **89** | 34,9% | 89/255 × 3,3 = **1,15 V** |
| 35,2 °C | 352 | (72 × 255)/100 = 18360/100 → **183** | 71,8% | 183/255 × 3,3 = **2,37 V** |

Hasil simulasi host: duty 89 dan 183 (sama dengan hitungan tangan). Bandingkan dengan `fan_pwm_pct` pada Serial
Monitor Wokwi Anda (harus 34,9 dan 71,8).

### C4 — Menganalisis
`bacaSoilMilliVoltsTerfilter()` adalah **moving average** dengan buffer melingkar. Fungsi ini membaca satu sampel
mV, menimpanya di `bufferMv[indeksFilter]`, memajukan indeks secara modulo `N`, lalu merata-ratakan isi buffer.
Selama belum penuh (`filterPenuh == false`) ia hanya merata-ratakan sampel yang sudah terisi, sehingga angka nol awal
tidak menarik hasil ke bawah.

| | N = 5 | N = 60 (interval 1 s) |
|---|---|---|
| Penurunan derau acak (≈ √N) | ≈ 2,2× | ≈ 7,7× |
| Tundaan rata-rata (≈ (N−1)/2 s) | ≈ 2 s | ≈ 29,5 s |
| Waktu penuh/stabil setelah lonjakan | 5 s | 60 s |
| Memori buffer | 20 B | 240 B |

Kompromi: N besar memberi sinyal lebih halus tetapi **lambat merespons**. Konsekuensinya, pompa baru mati
± 30 detik setelah tanah mencapai 60% (risiko kelebihan air) dan pengaman C6 dapat salah memicu fault. Jendela
15 detik C6 lebih pendek daripada tundaan filter, sehingga kenaikan `soil_pct` yang sudah terjadi belum terlihat.
Untuk tanah yang berubah lambat N = 5–10 sudah memadai.

### C5 — Mengevaluasi
Tanpa histeresis, relai berpindah 26 kali per 30 detik (≈ 0,87 klik/detik ≈ **74.880 klik/hari**). Umur kontak
relai tipikal sekitar 10⁵ operasi untuk beban listrik 220 V AC (pompa bersifat induktif, jadi lebih pendek) dan
sekitar 10⁷ operasi mekanis, sehingga umur listriknya habis dalam **± 1,3 hari** dan umur mekanisnya dalam
± 133 hari. Percikan busur pada kontak juga mempercepat aus dan menimbulkan derau EMI. Jika setiap perubahan status
dikirim sebagai pesan MQTT, beban naik menjadi ± 75 ribu pesan/hari (≈ 2,2 juta/bulan) dan cepat menghabiskan
kuota cloud. Dengan histeresis, derau ±1% menghasilkan 0 klik.

Celah (deadband) yang direkomendasikan: **± 25 poin (35%–60%)** untuk tanah kebun kopi. Aturan praktisnya,
lebar celah sedikitnya 2–3 kali puncak-ke-puncak derau sinyal terfilter (yang di sini < 5 poin) ditambah
*margin* untuk keterlambatan penyebaran air di tanah, saat sensor baru membaca basah beberapa menit setelah pompa
menyala. Celah yang terlalu sempit (< 10 poin) kembali memicu *short-cycling*, sedangkan celah yang terlalu lebar
(> 40 poin) membuat tanah terlalu kering sebelum disiram. Pertimbangkan juga waktu hidup minimum pompa (mis. 30 s).

### C6 — Mencipta
**Fitur:** Pump Run-Time Safety Cut-Off, tersedia di `firmware/src/main.cpp`.

| Parameter | Nilai |
|---|---|
| `BATAS_JALAN_POMPA_MS` | 15000 ms |
| `KENAIKAN_MIN_PCT` | 2,0 poin (batas "tidak kunjung naik") |
| Kedip peringatan | LED merah GPIO 27, 2 Hz, non-blocking |
| Reset fault | Tahan tombol kuning 3 detik (atau reset papan) |

**Cara kerja**
1. Saat pompa berpindah OFF → ON, `pompaRefMs` dan `pompaRefPct` dicatat.
2. Tiap siklus 1 detik, bila `soil_pct` naik ≥ 2 poin dari acuan maka acuan waktu dan nilai diperbarui (pompa sehat
   yang mengisi pelan tidak diputus).
3. Bila pompa menyala dan `sekarang − pompaRefMs > 15000 ms` tanpa kenaikan, maka `statusPompa = false`,
   `faultDryRun = true` (terkunci), dan event `{"event":"fault","fault":"DRY_RUN_PROTECTION"}` dikirim.
4. Selama fault: pompa dipaksa OFF (termasuk saat override manual), `ctrl_mode` = `FAULT`, `fault` =
   `DRY_RUN_PROTECTION` pada tiap telemetri, dan LED merah berkedip.
5. Tahan tombol 3 s untuk reset (`fault_cleared`).

**Potongan kode kunci**
```cpp
if (faultDryRun) statusPompa = false;                       // latched
...
if (statusPompa && !pompaSebelum) { pompaRefMs = sekarang; pompaRefPct = soilPct; }
if (statusPompa) {
  if (soilPct - pompaRefPct >= KENAIKAN_MIN_PCT) { pompaRefMs = sekarang; pompaRefPct = soilPct; }
  else if (sekarang - pompaRefMs > BATAS_JALAN_POMPA_MS) {
    statusPompa = false; faultDryRun = true;
    Serial.println("{\"event\":\"fault\",\"fault\":\"DRY_RUN_PROTECTION\",\"pump_on\":false}");
  }
}
```

**Bukti uji (simulasi host; log lengkap di `docs/bukti-uji-simulasi-host.txt`)**

| Skenario | Hasil |
|---|---|
| Tandon habis, `soil_pct` tetap 20% | Pompa ON pada t = 1 s, fault pada t = 17 s (jalan 16 s karena cuplikan 1 s dan syarat "lebih dari 15 s"), relai OFF, LED merah berkedip |
| `soil_pct` naik 1%/detik saat pompa jalan | Tidak ada fault |
| Override manual + sensor terlepas (80%) | Fault tetap terjadi, pompa OFF |
| DHT22 gagal baca | Pengaman tetap bekerja |
| Tahan tombol 3 s saat fault | `fault_cleared`, kendali normal kembali |

Waktu respons terburuk = 15 s + 1 siklus cuplik (≤ 16 s).

---

## 4. Verifikasi Mandiri Terhadap Ceklis Asisten

| No | Unjuk kerja | Status |
|---|---|---|
| 1 | Rangkaian pada pin aman dan terkompilasi tanpa galat | Kode dikompilasi pada simulator host (core v2 dan v3). **Kompilasi PlatformIO/Wokwi perlu dicek di PC Anda** |
| 2 | Sensor tanah terfilter dan pemetaan terbalik 0–100% | Terverifikasi (Tabel 3.1) |
| 3 | Demonstrasi chattering vs histeresis dan PWM kipas | Terverifikasi (Tabel 3.2, C3) |
| 4 | Fitur C6 berfungsi, kode dan draf laporan di-push | Fitur terverifikasi di simulasi host; **push ke Git dilakukan oleh kelompok** |

## 5. Bukti yang Wajib Anda Lampirkan Sendiri (`docs/img/`)
- `ss-tabel31-*.png`: Serial Monitor tiap posisi potensiometer
- `ss-chattering-mode0.png` dan `ss-histeresis-mode1.png`: nilai `relay_switches` setelah 30 detik
- `ss-pwm-31_5C.png` dan `ss-pwm-35_2C.png`: `fan_pwm_pct` dan kecerahan LED biru
- `ss-c6-fault.png`: JSON `"fault":"DRY_RUN_PROTECTION"` dan LED merah menyala

## 6. Kesimpulan
Kalibrasi dua titik dengan `analogReadMilliVolts()` dan moving average N = 5 memetakan 0–100% dengan benar.
Histeresis 35–60% menurunkan klik relai dari 26 menjadi 0–1 per 30 detik pada derau ±1%, sehingga memperpanjang umur
relai dan menekan beban MQTT. PWM LEDC memberi kendali kipas proporsional (duty 89 pada 31,5 °C dan 183 pada
35,2 °C). Pengaman C6 memutus pompa dalam ≤ 16 detik saat tidak ada kenaikan kelembapan.
