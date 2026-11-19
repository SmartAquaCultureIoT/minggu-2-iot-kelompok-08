// Stub Arduino minimal agar firmware/src/main.cpp bisa dikompilasi & diuji di PC (host).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#define HIGH 1
#define LOW 0
#define INPUT 0
#define INPUT_PULLUP 2
#define OUTPUT 1
#define ADC_11db 3
#define constrain(x,a,b) ((x)<(a)?(a):((x)>(b)?(b):(x)))
typedef uint8_t byte;
using std::isnan;
#define ESP_ARDUINO_VERSION_VAL(a,b,c) (((a)<<16)|((b)<<8)|(c))
#ifndef CORE_V3
#define ESP_ARDUINO_VERSION ESP_ARDUINO_VERSION_VAL(2,0,17)
#else
#define ESP_ARDUINO_VERSION ESP_ARDUINO_VERSION_VAL(3,0,0)
#endif
#ifdef CORE_V3
inline void ledcAttach(int, int, int) {}
#endif

extern unsigned long g_millis;
extern int g_pin[64];
extern uint32_t g_soil_mv;
extern uint8_t g_pwm;
extern std::vector<std::string> g_serial_lines;

inline unsigned long millis() { return g_millis; }
inline void pinMode(int, int) {}
inline int digitalRead(int p) { return g_pin[p]; }
inline void digitalWrite(int p, int v) { g_pin[p] = v; }
inline uint32_t analogReadMilliVolts(int) { return g_soil_mv; }
inline int analogRead(int) { return (int)(g_soil_mv * 4095.0 / 3300.0 + 0.5); }
inline void analogReadResolution(int) {}
inline void analogSetPinAttenuation(int, int) {}
inline void ledcSetup(int, int, int) {}
inline void ledcAttachPin(int, int) {}
inline void ledcWrite(int, int d) { g_pwm = (uint8_t)d; }
inline long map(long x, long a, long b, long c, long d) { return (x - a) * (d - c) / (b - a) + c; }

class String {
 public:
  std::string s;
  String(float v, int dec) { char b[32]; snprintf(b, sizeof b, "%.*f", dec, v); s = b; }
};

struct SerialStub {
  std::string cur;
  void begin(int) {}
  void print(const std::string& x) { cur += x; }
  void println(const char* x) { cur += x; flush(); }
  void println() { flush(); }
  void printf(const char* f, ...) {
    char b[512]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a);
    cur += b; if (!cur.empty() && cur.back() == '\n') { cur.pop_back(); flush(); }
  }
  void flush() { char b[24]; snprintf(b, sizeof b, "%6lu ms | ", g_millis); g_serial_lines.push_back(std::string(b) + cur); cur.clear(); }
};
extern SerialStub Serial;
