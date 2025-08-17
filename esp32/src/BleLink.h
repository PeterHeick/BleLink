#ifndef BLE_LINK_H
#define BLE_LINK_H

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>

/**
 * BleLink — generisk BLE transport over Nordic UART Service (NUS).
 * Framing: én linje pr. besked, afsluttet med '\n'.
 *
 * Ved modtagelse:
 *   - Er linjen gyldig JSON -> onReceiveJson(doc) kaldes
 *   - Ellers -> onReceiveRaw(line) kaldes
 *
 * Afsendelse:
 *   - sendJson(doc): sender JSON som én linje
 *   - sendRaw(cstr): sender rå tekstlinje som den er (tilføjer '\n' hvis mangler)
 */
class BleLink {
public:
  using JsonCb = std::function<void(const JsonDocument& doc)>;
  using RawCb  = std::function<void(const String& line)>;

  explicit BleLink(const char* deviceName = "BleLink-Device");

  void setup();      // kald i setup()
  void loop();       // kald i loop() for vedligehold
  void disconnect(); // pæn nedlukning (valgfri)

  bool isConnected() const;

  // Afsendelse
  void sendJson(const JsonDocument& doc);
  void sendRaw(const char* cstr);

  // Modtagelse
  void onReceiveJson(JsonCb cb);
  void onReceiveRaw(RawCb cb);

private:
  void _initializeBLE();
  void _sendLine(const char* cstr);
  void _emitJson(const JsonDocument& doc);
  void _emitRaw(const String& line);

  char   _name[32] = {0};
  JsonCb _jsonCb   = nullptr;
  RawCb  _rawCb    = nullptr;
};

#endif // BLE_LINK_H
