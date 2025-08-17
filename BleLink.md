# BleLink – generisk BLE-link mellem ESP32 og Python

**BleLink** er et lille, genbrugeligt transportlag til **Bluetooth Low Energy** (BLE) baseret på **Nordic UART Service (NUS)**. Det giver en stabil, enkel kanal mellem en ESP32 og en host (Python), med **linje-termineret** framing: én besked per linje, afsluttet med `\n`.

- **Ingen protokol-krav.** Du bestemmer selv indholdet (JSON eller rå tekst).  
- **Let at teste.** Du kan sende/aflæse data fra begge sider på få linjer.  
- **Robust.** ESP32 håndterer reconnects; Python har robust `connect()` med retry.

---

## Overblik

- **Framing:** Én besked = én linje = bytes indtil `\n`.  
- **Indhold:**  
  - **JSON** (gyldig JSON-linje) → leveres som *parsed objekt* til callback  
  - **Rå tekst** (ikke-JSON) → leveres som *ren tekstlinje* til callback  
- **Retning:**  
  - ESP32 → Python: `sendJson(doc)` / `sendRaw(text)`  
  - Python → ESP32: `send_json(obj)` / `send_raw(text)`  
- **UUID’er (NUS):**  
  - Service: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`  
  - RX (host→ESP32 / write): `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`  
  - TX (ESP32→host / notify): `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`

---

## Mappestruktur (foreslået)

```
BleLink/
├─ esp32/
│  ├─ platformio.ini
│  └─ src/
│     ├─ BleLink.h
│     ├─ BleLink.cpp
│     └─ main.cpp        # demo
└─ python/
   └─ ble_link.py        # demo indbygget i filens bund
```

---

## ESP32-delen

### Afhængigheder (PlatformIO)

`esp32/platformio.ini`
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

upload_speed = 115200
monitor_speed = 115200

lib_deps =
  https://github.com/h2zero/NimBLE-Arduino.git#1.4.2
  https://github.com/bblanchon/ArduinoJson.git#v7.0.0
```

### Offentligt API (ESP32)

`BleLink.h` (essensen)
```cpp
class BleLink {
public:
  using JsonCb = std::function<void(const JsonDocument& doc)>;
  using RawCb  = std::function<void(const String& line)>;

  explicit BleLink(const char* deviceName = "BleLink-Device");

  void setup();          // kaldes i Arduino setup()
  void loop();           // kaldes i Arduino loop()
  void disconnect();     // valgfri, pæn nedlukning
  bool isConnected() const;

  // Afsend
  void sendJson(const JsonDocument& doc);
  void sendRaw(const char* cstr);

  // Modtag
  void onReceiveJson(JsonCb cb);
  void onReceiveRaw(RawCb cb);
};
```

---

## Python-delen

### Installation

```bash
python -m venv venv
source venv/bin/activate
pip install bleak
```

### Offentligt API (Python)

```python
class BleLink:
    def __init__(self, device_name: str): ...
    async def connect(self, timeout: float = 20.0, scan_timeout: float = 12.0): ...
    async def disconnect(self): ...
    def is_connected(self) -> bool: ...
    async def send_json(self, obj: Dict[str, Any], response: bool=True): ...
    async def send_raw(self, text: str, response: bool=True): ...
    def on_receive_json(self, cb: Callable[[Dict[str, Any]], None]): ...
    def on_receive_raw(self, cb: Callable[[str], None]): ...
```

---

## Best practices og FAQ

- Sørg for unikke `device_name` for hvert ESP32 modul.  
- JSON er valgfrit – men gør parsing nemmere.  
- Til test kan du køre flere Python-klienter efter hinanden, men ikke parallelt.  
- Hvis du mister forbindelse, kalder ESP32 automatisk advertising igen.  
- På Python-siden kan du lave reconnect-loops, hvis `connect()` fejler.
