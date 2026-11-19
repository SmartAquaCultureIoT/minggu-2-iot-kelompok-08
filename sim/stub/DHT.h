#pragma once
#include "Arduino.h"
#define DHT22 22
extern float g_temp, g_hum;
class DHT {
 public:
  DHT(int, int) {}
  void begin() {}
  float readTemperature() { return g_temp; }
  float readHumidity() { return g_hum; }
};
