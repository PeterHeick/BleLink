#include <Arduino.h>
#include "BleLink.h"

BleLink bleLink("BLE-LINK-TEST");

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.println("\n--- BleLink Demo (ESP32 -> Python) ---");

  // Modtag JSON fra Python
  bleLink.onReceiveJson([](const JsonDocument& doc){
    Serial.print("[RX:JSON] ");
    String s; serializeJson(doc, s);
    Serial.println(s);

    // Eksempel: hvis Python sender {"op":"echo","msg":"..."}
    const char* op = doc["op"] | "";
    if (strcmp(op, "echo") == 0) {
      JsonDocument reply;
      reply["from"] = "esp32";
      reply["echo"] = doc["msg"] | "";
      bleLink.sendJson(reply);  // ESP32 -> Python
    }
  });

  // Modtag rå tekst fra Python
  bleLink.onReceiveRaw([](const String& line){
    Serial.printf("[RX:RAW ] %s\n", line.c_str());
    if (line == "PING") {
      bleLink.sendRaw("PONG");  // ESP32 -> Python (rå tekst)
    }
  });

  bleLink.setup();
}

void loop() {
  bleLink.loop();

  // ESP32 -> Python: send en status-JSON hvert 5. sekund
  static uint32_t last = 0;
  if (millis() - last > 5000 && bleLink.isConnected()) {
    last = millis();
    JsonDocument j;
    j["from"] = "esp32";
    j["event"] = "status";
    j["uptime_ms"] = (uint32_t)millis();
    j["note"] = "periodic status from esp32";
    bleLink.sendJson(j);
  }

  delay(5);
}
