#pragma once
#include "Arduino.h"
struct Raw { std::string s; };
inline Raw serialized(const String& x) { return Raw{x.s}; }
class JsonDocument {
  std::vector<std::pair<std::string, std::string>> kv;
  struct Proxy {
    JsonDocument& d; std::string k;
    void set(const std::string& v) { d.kv.push_back({k, v}); }
    Proxy& operator=(int v) { set(std::to_string(v)); return *this; }
    Proxy& operator=(unsigned long v) { set(std::to_string(v)); return *this; }
    Proxy& operator=(uint32_t v) { set(std::to_string(v)); return *this; }
    Proxy& operator=(bool v) { set(v ? "true" : "false"); return *this; }
    Proxy& operator=(const char* v) { set(std::string("\"") + v + "\""); return *this; }
    Proxy& operator=(const Raw& v) { set(v.s); return *this; }
  };
 public:
  Proxy operator[](const char* k) { return Proxy{*this, k}; }
  void out(SerialStub& s) {
    std::string r = "{";
    for (size_t i = 0; i < kv.size(); i++) { if (i) r += ","; r += "\"" + kv[i].first + "\":" + kv[i].second; }
    s.print(r + "}");
  }
};
inline void serializeJson(JsonDocument& d, SerialStub& s) { d.out(s); }
