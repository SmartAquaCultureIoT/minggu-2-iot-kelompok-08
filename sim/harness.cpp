// Harness uji host: menjalankan firmware/src/main.cpp ASLI dengan stub Arduino.
#include "Arduino.h"
#include "DHT.h"
#include <cstdlib>
#include <cstring>
unsigned long g_millis = 0; int g_pin[64] = {0}; uint32_t g_soil_mv = 2650; uint8_t g_pwm = 0;
std::vector<std::string> g_serial_lines; SerialStub Serial; float g_temp = 30.0f, g_hum = 70.0f;
#include "../firmware/src/main.cpp"

static uint32_t mvOf(double pct) { return (uint32_t)(2650.0 - pct * 15.5 + 0.5); }
static void step(unsigned long ms) { for (unsigned long t = 0; t < ms; t += 10) { g_millis += 10; loop(); } }
static void flushLog(size_t& from, const char* tag) {
  for (; from < g_serial_lines.size(); from++) printf("[%s] %s\n", tag, g_serial_lines[from].c_str());
}
static long switches() {  // baca relay_switches dari JSON terakhir
  for (int i = (int)g_serial_lines.size() - 1; i >= 0; i--) {
    size_t p = g_serial_lines[i].find("\"relay_switches\":");
    if (p != std::string::npos) return atol(g_serial_lines[i].c_str() + p + 17);
  }
  return -1;
}
static std::string lastJson() {
  for (int i = (int)g_serial_lines.size() - 1; i >= 0; i--)
    if (g_serial_lines[i].find("device_id") != std::string::npos) return g_serial_lines[i];
  return "";
}

int main(int argc, char** argv) {
  const char* m = argc > 1 ? argv[1] : "t31";
  g_pin[15] = 1;  // tombol dilepas (INPUT_PULLUP = HIGH)
  setup();
  if (!strcmp(m, "t31")) {                       // Tabel 3.1: urut kering -> basah
    double pts[5] = {0, 30, 50, 70, 100};
    for (double p : pts) { g_soil_mv = mvOf(p); step(10000); printf("%s\n", lastJson().c_str()); }
    printf("--- titik 50%% didekati dari sisi BASAH ---\n");
    g_soil_mv = mvOf(100); step(10000); g_soil_mv = mvOf(50); step(10000); printf("%s\n", lastJson().c_str());
  } else if (!strcmp(m, "wobble")) {             // wobble center amp mode(alt|rnd) seed
    double c = atof(argv[2]), a = atof(argv[3]); bool rnd = !strcmp(argv[4], "rnd"); srand(atoi(argv[5]));
    g_soil_mv = mvOf(c + 1); step(6000);        // pemanasan filter
    long awal = switches();
    for (int s = 0; s < 30; s++) {
      double v = rnd ? c + a * (2.0 * rand() / RAND_MAX - 1.0) : c + (s % 2 ? a : -a);
      g_soil_mv = mvOf(v); step(1000);
    }
    printf("%ld\n", switches() - awal);
  } else if (!strcmp(m, "c6")) {                 // C6: tandon habis, soil tetap 20%
    size_t from = 0; g_soil_mv = mvOf(20);
    int ledToggle = 0, prevLed = 0; bool faultSeen = false; unsigned long tFault = 0;
    for (unsigned long t = 0; t < 30000; t += 10) {
      step(10);
      if (g_pin[27] != prevLed) { ledToggle++; prevLed = g_pin[27]; }
      if (!faultSeen && g_pin[27] == 1) { faultSeen = true; tFault = g_millis; }
    }
    flushLog(from, "C6");
    printf("LED merah toggle=%d (fault pertama t=%lu ms), relai=%d\n", ledToggle, tFault, g_pin[26]);
    printf("--- uji reset: tahan tombol 3 s ---\n");
    g_pin[15] = 0; step(3200); g_pin[15] = 1; g_soil_mv = mvOf(50); step(3000); flushLog(from, "RST");
  } else if (!strcmp(m, "c6rise")) {             // pompa menyala & soil naik wajar -> TIDAK fault
    size_t from = 0; double p = 20; g_soil_mv = mvOf(p); step(1000);
    for (int s = 0; s < 40; s++) { p += 1.0; g_soil_mv = mvOf(p); step(1000); }
    flushLog(from, "RISE"); printf("%s\n", lastJson().c_str());
  } else if (!strcmp(m, "c6override")) {         // override manual + sensor terlepas -> fault
    size_t from = 0; g_soil_mv = mvOf(80); step(2000);
    g_pin[15] = 0; step(100); g_pin[15] = 1; step(20000); flushLog(from, "OVR");
  } else if (!strcmp(m, "c6dhtfail")) {          // DHT gagal tidak boleh melumpuhkan pengaman
    size_t from = 0; g_temp = NAN; g_soil_mv = mvOf(20); step(20000); flushLog(from, "DHT");
    printf("relai=%d fault_led=%d\n", g_pin[26], g_pin[27]);
  } else if (!strcmp(m, "pwm")) {                // C3
    float ts[] = {26, 28, 30, 31.5f, 33, 35.2f, 36, 38, 40};
    for (float t : ts) { g_temp = t; step(1000); printf("T=%.1f duty=%d\n", t, g_pwm); }
  }
  return 0;
}
