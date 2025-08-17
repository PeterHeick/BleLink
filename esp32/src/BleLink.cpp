#include "BleLink.h"
#include <NimBLEDevice.h>
#include <string>
#include <cstring>

// --- NUS UUIDs ---
#define NUS_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_CHAR_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Write host->ESP32
#define NUS_CHAR_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // Notify ESP32->host

// --- NimBLE globals ---
static NimBLEServer*         g_server     = nullptr;
static NimBLECharacteristic* g_tx         = nullptr;
static bool                  g_connected  = false;
static volatile bool         g_needReinit = false;
static std::string           g_rxBuf;

// --- helpers ---
static void onServerConnected(NimBLEServer* s) {
  static uint32_t lastConn = 0;
  if (millis() - lastConn < 300) return;  // debounce
  lastConn = millis();

  g_connected  = true;
  g_needReinit = false;
  if (s) s->getAdvertising()->stop();
  Serial.println("[BleLink] Connected");
}

static void onServerDisconnected() {
  static uint32_t lastDisc = 0;
  if (millis() - lastDisc < 300) return;  // debounce
  lastDisc = millis();

  g_connected = false;
  g_rxBuf.clear();
  Serial.println("[BleLink] Disconnected -> restart advertising");
  NimBLEDevice::getAdvertising()->start();
  g_needReinit = true; // “ren” reinit i loop()
}

static void handleWrite(NimBLECharacteristic* ch,
                        std::function<void(const JsonDocument&)> emitJson,
                        std::function<void(const String&)> emitRaw) {
  if (!ch) return;
  std::string chunk = ch->getValue();
  if (chunk.empty()) return;

  g_rxBuf.append(chunk);
  size_t pos;
  while ((pos = g_rxBuf.find('\n')) != std::string::npos) {
    std::string line = g_rxBuf.substr(0, pos);
    g_rxBuf.erase(0, pos + 1);

    // Prøv JSON
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, line);
    if (!err) {
      emitJson(doc);
    } else {
      emitRaw(String(line.c_str()));
    }
  }
}

// --- callbacks (uden override for kompatibilitet) ---
class ServerCallbacks : public NimBLEServerCallbacks {
public:
  void onConnect(NimBLEServer* s) { onServerConnected(s); }
  void onConnect(NimBLEServer* s, ble_gap_conn_desc* /*d*/) { onServerConnected(s); }
  void onDisconnect(NimBLEServer* /*s*/) { onServerDisconnected(); }
  void onDisconnect(NimBLEServer* /*s*/, ble_gap_conn_desc* /*d*/) { onServerDisconnected(); }
};

class CharCallbacks : public NimBLECharacteristicCallbacks {
public:
  CharCallbacks(std::function<void(const JsonDocument&)> j,
                std::function<void(const String&)> r)
  : _emitJson(std::move(j)), _emitRaw(std::move(r)) {}

  void onWrite(NimBLECharacteristic* c) { handleWrite(c, _emitJson, _emitRaw); }
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& /*i*/) { handleWrite(c, _emitJson, _emitRaw); }

private:
  std::function<void(const JsonDocument&)> _emitJson;
  std::function<void(const String&)>       _emitRaw;
};

// --- BleLink impl ---
BleLink::BleLink(const char* deviceName) {
  strncpy(_name, deviceName, sizeof(_name)-1);
  _name[sizeof(_name)-1] = '\0';
}

void BleLink::setup() { _initializeBLE(); }

void BleLink::loop() {
  if (g_connected && g_server && g_server->getConnectedCount() == 0) {
    Serial.println("[BleLink] Link lost w/o callback -> reinit");
    g_connected  = false;
    g_needReinit = true;
  }
  if (g_needReinit) {
    g_needReinit = false;
    delay(150);
    NimBLEDevice::deinit();
    delay(250);
    _initializeBLE();
  }
}

void BleLink::disconnect() {
  // (intet hårdt stop krævet; reinit håndteres i loop)
}

bool BleLink::isConnected() const { return g_connected; }

void BleLink::sendJson(const JsonDocument& doc) {
  String s; serializeJson(doc, s);
  if (!s.endsWith("\n")) s += "\n";
  _sendLine(s.c_str());
}

void BleLink::sendRaw(const char* cstr) {
  if (!cstr) return;
  String s(cstr);
  if (!s.endsWith("\n")) s += "\n";
  _sendLine(s.c_str());
}

void BleLink::onReceiveJson(JsonCb cb) { _jsonCb = std::move(cb); }
void BleLink::onReceiveRaw (RawCb  cb) { _rawCb  = std::move(cb); }

void BleLink::_emitJson(const JsonDocument& doc) { if (_jsonCb) _jsonCb(doc); }
void BleLink::_emitRaw (const String& line)      { if (_rawCb)  _rawCb(line); }

void BleLink::_initializeBLE() {
  static ServerCallbacks srvCb;
  static CharCallbacks   chCb([this](const JsonDocument& d){ _emitJson(d); },
                              [this](const String& s){ _emitRaw(s); });

  NimBLEDevice::init(_name);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setMTU(247);

  g_server = NimBLEDevice::createServer();
  g_server->setCallbacks(&srvCb);

  NimBLEService* svc = g_server->createService(NUS_SERVICE_UUID);
  g_tx = svc->createCharacteristic(NUS_CHAR_TX_UUID, NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* rx = svc->createCharacteristic(
    NUS_CHAR_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(&chCb);

  svc->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setName(_name);
  adv->addServiceUUID(svc->getUUID());
  adv->start();

  Serial.println("[BleLink] Advertising started");
}

void BleLink::_sendLine(const char* cstr) {
  if (!g_connected || !g_tx || !cstr) return;
  const char* s = cstr;
  const size_t CHUNK = 20; // MTU-safe
  size_t len = strlen(s);
  for (size_t i = 0; i < len; i += CHUNK) {
    size_t n = (len - i < CHUNK) ? (len - i) : CHUNK;
    g_tx->setValue((const uint8_t*)(s + i), n);
    g_tx->notify();
    delay(2);
  }
}
