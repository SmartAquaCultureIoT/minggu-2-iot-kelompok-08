// Lab-1 Minggu 2 - Teknologi IoT (Prodi S1 Informatika FT UNIB)
// Pengendali mikro-iklim greenhouse kopi:
//   ADC1 terkalibrasi + moving average + kalibrasi dua titik terbalik,
//   histeresis relai pompa, PWM kipas proporsional suhu,
//   dan (C6) pengaman waktu jalan maksimum pompa "DRY_RUN_PROTECTION".
#include <Arduino.h>
#include <DHT.h>
#include <ArduinoJson.h>

// --- Definisi Pin Aman ESP32 (Bebas Konflik ADC2 & Strapping GPIO 12) ---
#define PIN_DHT 4
#define TIPE_DHT DHT22
#define PIN_SOIL_ADC1 34    // Input-only ADC1_CH6 (aman saat Wi-Fi aktif)
#define PIN_TOMBOL_MODE 15  // Pushbutton INPUT_PULLUP
#define PIN_RELAY_POMPA 26  // Output penggerak relai pompa & LED hijau
#define PIN_PWM_KIPAS 18    // Output PWM kecepatan kipas / LED biru
#define PIN_LED_FAULT 27    // (C6) LED merah peringatan fault, pin serbaguna aman

// Ubah ke 0 untuk melihat gejala CHATTERING (1 ambang), 1 untuk HISTERESIS (2 ambang)
#ifndef GUNAKAN_HISTERESIS
#define GUNAKAN_HISTERESIS 1
#endif

// (C6) 1 = pengaman dry-run aktif (default, wajib aktif saat verifikasi asisten).
// Set 0 HANYA saat Eksperimen 2 agar hitungan relay_switches murni dari algoritma
// kendali (di potensiometer, "tanah" tidak benar-benar basah saat pompa menyala).
#ifndef PENGAMAN_DRY_RUN
#define PENGAMAN_DRY_RUN 1
#endif

// Konstanta Kalibrasi Dua Titik Sensor Tanah Kapasitif (dalam milivolt)
const int MV_TANAH_KERING = 2650;  // Tegangan saat media kering (0% lengas)
const int MV_TANAH_BASAH = 1100;   // Tegangan saat jenuh air (100% lengas)

// Ambang Kendali Histeresis Pompa Irigasi (%)
const float AMBANG_POMPA_ON = 35.0f;
const float AMBANG_POMPA_OFF = 60.0f;
const float AMBANG_TUNGGAL = 45.0f;  // Dipakai hanya jika GUNAKAN_HISTERESIS == 0

// Konfigurasi PWM Perangkat Keras LEDC (5 kHz, Resolusi 8-bit: 0 - 255)
const int PWM_FREQ_HZ = 5000;
const int PWM_RES_BIT = 8;
const int PWM_KANAL = 0;

// Buffer Moving Average (Jendela Geser N = 5 Sampel)
const int UKURAN_FILTER = 5;
uint32_t bufferMv[UKURAN_FILTER] = {0};
int indeksFilter = 0;
bool filterPenuh = false;

// --- (C6) Pengaman Waktu Jalan Maksimum Pompa ---
const unsigned long BATAS_JALAN_POMPA_MS = 15000;  // maks. 15 detik tanpa kemajuan
const float KENAIKAN_MIN_PCT = 2.0f;               // kenaikan soil_pct yang dianggap "naik"
const unsigned long INTERVAL_KEDIP_MS = 250;       // kedip peringatan 2 Hz
const unsigned long TAHAN_RESET_FAULT_MS = 3000;   // tahan tombol 3 detik = reset fault

DHT dht(PIN_DHT, TIPE_DHT);
unsigned long waktuTelemetriTerakhir = 0;
unsigned long waktuDebounceTombol = 0;
const unsigned long INTERVAL_BACA_MS = 1000;

bool statusPompa = false;
bool modeOverridePaksa = false;
bool tombolSebelumnya = HIGH;
unsigned long seqPaket = 0;
unsigned long hitungTransisiRelai = 0;

bool faultDryRun = false;             // terkunci (latched) sampai di-reset
unsigned long pompaRefMs = 0;         // acuan waktu: pompa mulai / terakhir ada kenaikan
float pompaRefPct = 0.0f;             // acuan soil_pct pada saat acuan waktu dicatat
bool menahTombolReset = false;
unsigned long waktuTekanTombol = 0;
unsigned long waktuKedipTerakhir = 0;
bool keadaanKedip = false;

void setupPwmKipas() {
#if defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0))
  ledcAttach(PIN_PWM_KIPAS, PWM_FREQ_HZ, PWM_RES_BIT);
#else
  ledcSetup(PWM_KANAL, PWM_FREQ_HZ, PWM_RES_BIT);
  ledcAttachPin(PIN_PWM_KIPAS, PWM_KANAL);
#endif
}

void tulisPwmKipas(uint8_t duty) {
#if defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0))
  ledcWrite(PIN_PWM_KIPAS, duty);
#else
  ledcWrite(PWM_KANAL, duty);
#endif
}

uint32_t bacaSoilMilliVoltsTerfilter() {
  uint32_t mvMentah = analogReadMilliVolts(PIN_SOIL_ADC1);
  bufferMv[indeksFilter] = mvMentah;
  indeksFilter = (indeksFilter + 1) % UKURAN_FILTER;
  if (indeksFilter == 0) filterPenuh = true;

  int jumlahSampel = filterPenuh ? UKURAN_FILTER : indeksFilter;
  uint64_t total = 0;
  for (int i = 0; i < jumlahSampel; i++) total += bufferMv[i];
  return (uint32_t)(total / jumlahSampel);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SOIL_ADC1, INPUT);
  pinMode(PIN_TOMBOL_MODE, INPUT_PULLUP);
  pinMode(PIN_RELAY_POMPA, OUTPUT);
  pinMode(PIN_LED_FAULT, OUTPUT);
  digitalWrite(PIN_RELAY_POMPA, LOW);
  digitalWrite(PIN_LED_FAULT, LOW);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_SOIL_ADC1, ADC_11db);

  setupPwmKipas();
  dht.begin();
  Serial.println("{\"event\":\"boot\",\"lab\":\"Lab-01 Greenhouse Kopi UNIB\"}");
}

void loop() {
  unsigned long sekarang = millis();

  // 1. Pembacaan Tombol Non-Blocking (Debounce 60 ms)
  //    - Normal : tekan = toggle override manual.
  //    - Fault  : tahan 3 detik = reset fault DRY_RUN_PROTECTION.
  bool bacaTombol = digitalRead(PIN_TOMBOL_MODE);
  if (bacaTombol == LOW && tombolSebelumnya == HIGH && (sekarang - waktuDebounceTombol > 60)) {
    waktuDebounceTombol = sekarang;
    if (faultDryRun) {
      menahTombolReset = true;
      waktuTekanTombol = sekarang;
    } else {
      modeOverridePaksa = !modeOverridePaksa;
      Serial.printf("{\"event\":\"mode_change\",\"manual_override\":%s}\n",
                    modeOverridePaksa ? "true" : "false");
    }
  }
  if (bacaTombol == HIGH) menahTombolReset = false;
  if (faultDryRun && menahTombolReset && (sekarang - waktuTekanTombol >= TAHAN_RESET_FAULT_MS)) {
    faultDryRun = false;
    menahTombolReset = false;
    modeOverridePaksa = false;
    Serial.println("{\"event\":\"fault_cleared\",\"fault\":\"NONE\"}");
  }
  tombolSebelumnya = bacaTombol;

  // 2. (C6) Kedip peringatan non-blocking pada LED merah selama fault aktif
  if (faultDryRun) {
    if (sekarang - waktuKedipTerakhir >= INTERVAL_KEDIP_MS) {
      waktuKedipTerakhir = sekarang;
      keadaanKedip = !keadaanKedip;
      digitalWrite(PIN_LED_FAULT, keadaanKedip ? HIGH : LOW);
    }
  } else if (keadaanKedip) {
    keadaanKedip = false;
    digitalWrite(PIN_LED_FAULT, LOW);
  }

  // 3. Akuisisi Sensor & Evaluasi Kendali Aktuator Berkala (1000 ms)
  if (sekarang - waktuTelemetriTerakhir >= INTERVAL_BACA_MS) {
    waktuTelemetriTerakhir = sekarang;

    int adcRaw = analogRead(PIN_SOIL_ADC1);
    uint32_t soilMv = bacaSoilMilliVoltsTerfilter();
    float soilPct = (float)(MV_TANAH_KERING - (int)soilMv) * 100.0f
                    / (float)(MV_TANAH_KERING - MV_TANAH_BASAH);
    soilPct = constrain(soilPct, 0.0f, 100.0f);

    // Evaluasi Kendali Pompa: Fault > Override Manual > Histeresis / Ambang Tunggal
    bool pompaSebelum = statusPompa;
    if (faultDryRun) {
      statusPompa = false;  // fault terkunci: pompa wajib mati, termasuk saat override
    } else if (modeOverridePaksa) {
      statusPompa = true;
    } else {
#if GUNAKAN_HISTERESIS == 1
      if (soilPct < AMBANG_POMPA_ON) statusPompa = true;
      else if (soilPct >= AMBANG_POMPA_OFF) statusPompa = false;
#else
      statusPompa = (soilPct < AMBANG_TUNGGAL);
#endif
    }

    // (C6) Pengaman Waktu Jalan Maksimum: pompa menyala > 15 s tanpa kenaikan soil_pct
    if (statusPompa && !pompaSebelum) {
      pompaRefMs = sekarang;
      pompaRefPct = soilPct;
    }
#if PENGAMAN_DRY_RUN == 1
    if (statusPompa) {
      if (soilPct - pompaRefPct >= KENAIKAN_MIN_PCT) {
        pompaRefMs = sekarang;  // ada kemajuan: mulai hitung ulang
        pompaRefPct = soilPct;
      } else if (sekarang - pompaRefMs > BATAS_JALAN_POMPA_MS) {
        statusPompa = false;
        faultDryRun = true;
        keadaanKedip = false;
        waktuKedipTerakhir = sekarang - INTERVAL_KEDIP_MS;  // kedip langsung mulai
        Serial.println("{\"event\":\"fault\",\"fault\":\"DRY_RUN_PROTECTION\",\"pump_on\":false}");
      }
    }
#endif

    if (statusPompa != pompaSebelum) hitungTransisiRelai++;
    digitalWrite(PIN_RELAY_POMPA, statusPompa ? HIGH : LOW);
    unsigned long pompaJalanS = statusPompa ? (sekarang - pompaRefMs) / 1000UL : 0;

    // Sensor udara dibaca SETELAH kendali pompa agar kegagalan DHT22 tidak
    // melumpuhkan pengaman pompa.
    float suhuUdara = dht.readTemperature();
    float lembapUdara = dht.readHumidity();
    if (isnan(suhuUdara) || isnan(lembapUdara)) {
      Serial.println("{\"error\":\"Pembacaan DHT22 gagal\"}");
      return;
    }

    // Evaluasi Kendali PWM Kipas Proporsional Suhu (28.0 C -> 0%, 38.0 C -> 100%)
    int nilaiDuty = map((long)(suhuUdara * 10.0f), 280, 380, 0, 255);
    nilaiDuty = constrain(nilaiDuty, 0, 255);
    tulisPwmKipas((uint8_t)nilaiDuty);
    float fanPct = (nilaiDuty * 100.0f) / 255.0f;

    // Serialisasi Payload Telemetri JSON
    JsonDocument doc;
    doc["device_id"] = "esp32-kopi-gh01";
    doc["seq"] = ++seqPaket;
    doc["adc_raw"] = adcRaw;
    doc["soil_mv_avg"] = soilMv;
    doc["soil_pct"] = serialized(String(soilPct, 1));
    doc["temp_c"] = serialized(String(suhuUdara, 1));
    doc["air_hum_pct"] = serialized(String(lembapUdara, 1));
    doc["pump_on"] = statusPompa;
    doc["pump_run_s"] = pompaJalanS;
    doc["relay_switches"] = hitungTransisiRelai;
    doc["fan_pwm_pct"] = serialized(String(fanPct, 1));
    doc["ctrl_mode"] = faultDryRun ? "FAULT"
                       : (modeOverridePaksa ? "MANUAL"
                                            : (GUNAKAN_HISTERESIS ? "HYSTERESIS" : "SINGLE_THR"));
    doc["fault"] = faultDryRun ? "DRY_RUN_PROTECTION" : "NONE";

    serializeJson(doc, Serial);
    Serial.println();
  }
}
