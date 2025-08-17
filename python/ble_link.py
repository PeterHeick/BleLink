import asyncio
import json
from typing import Callable, Optional, Dict, Any
from bleak import BleakScanner, BleakClient
from bleak.exc import BleakError

# NUS UUIDs (skal matche ESP32)
SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  # notify ESP32->host
RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  # write  host->ESP32


class BleLink:
    """
    Generisk BLE link (Nordic UART Service) med line-delimited framing.
    Modtagelse:
      - on_receive_json(cb: dict -> None)
      - on_receive_raw(cb: str -> None)
      - on_receive(cb: (type, payload) -> None)  # kompatibilitet
        * Hvis JSON indeholder 'type', kaldes cb(type, payload)
        * Ellers cb(None, obj)

    Afsendelse:
      - await send_json(dict)
      - await send_raw(str)
      - await send(command, payload=None)  # convenience wrapper
    """

    def __init__(self, device_name: str):
        self.device_name = device_name
        self._client: Optional[BleakClient] = None
        self._tx_char = None
        self._rx_char = None
        self._rxbuf = bytearray()

        # callbacks
        self._cb_json: Optional[Callable[[Dict[str, Any]], None]] = None
        self._cb_raw:  Optional[Callable[[str], None]] = None
        self._cb_pair: Optional[Callable[[Optional[str], Any], None]] = None

    # ---------- public API ----------

    def on_receive_json(self, cb: Callable[[Dict[str, Any]], None]) -> None:
        self._cb_json = cb

    def on_receive_raw(self, cb: Callable[[str], None]) -> None:
        self._cb_raw = cb

    def on_receive(self, cb: Callable[[Optional[str], Any], None]) -> None:
        """Kompat: cb(type, payload). type=None hvis ikke tilstede i JSON."""
        self._cb_pair = cb

    def is_connected(self) -> bool:
        return bool(self._client and self._client.is_connected)

    async def connect(
        self,
        attempts: int = 3,
        delay: float = 1.5,
        timeout: float = 20.0,
        scan_timeout: float = 12.0,
    ) -> None:
        """
        Robust connect: prøv flere gange med lille pause imellem.
        """
        last_err: Exception | None = None
        for i in range(1, max(1, attempts) + 1):
            try:
                await self._connect_once(timeout=timeout, scan_timeout=scan_timeout)
                return
            except (BleakError, RuntimeError) as e:
                last_err = e
                if i < attempts:
                    print(f"[BleLink] connect-forsøg {i} fejlede: {e}")
                    await asyncio.sleep(max(0.0, delay))
        raise RuntimeError(f"BleLink: Kunne ikke forbinde efter {attempts} forsøg") from last_err

    async def disconnect(self) -> None:
        if not self._client:
            return
        try:
            if self._tx_char:
                await self._client.stop_notify(self._tx_char)
        except Exception:
            pass
        try:
            await self._client.disconnect()
        except Exception:
            pass
        finally:
            self._client = None
            self._tx_char = None
            self._rx_char = None
            self._rxbuf.clear()

    # ---- send ----
    async def send_json(self, obj: Dict[str, Any], response: bool = True) -> None:
        if not (self._client and self._client.is_connected and self._rx_char):
            raise RuntimeError("Ikke forbundet.")
        raw = (json.dumps(obj, separators=(",", ":")) + "\n").encode("utf-8")
        await self._client.write_gatt_char(self._rx_char, raw, response=response)

    async def send_raw(self, text: str, response: bool = True) -> None:
        if not (self._client and self._client.is_connected and self._rx_char):
            raise RuntimeError("Ikke forbundet.")
        if not text.endswith("\n"):
            text += "\n"
        await self._client.write_gatt_char(self._rx_char, text.encode("utf-8"), response=response)

    async def send(self, command: str, payload: Optional[Dict[str, Any]] = None, response: bool = True) -> None:
        """
        Convenience: send som {"command": ..., "payload": {...}}
        (Bevarer din eksisterende demo-kode kompatibel.)
        """
        await self.send_json({"command": command, "payload": (payload or {})}, response=response)

    # ---------- intern ----------

    async def _connect_once(self, timeout: float, scan_timeout: float) -> None:
        dev = await BleakScanner.find_device_by_name(self.device_name, timeout=scan_timeout)
        if not dev:
            raise RuntimeError(f"Enhed '{self.device_name}' ikke fundet (scan timeout).")

        client = BleakClient(dev, timeout=timeout)
        await client.connect()
        self._client = client

        # Find præcis NUS-service → karakteristika (undgår “multiple char with same UUID”)
        self._tx_char = self._rx_char = None
        for svc in client.services:
            if str(svc.uuid).lower() == SERVICE_UUID.lower():
                t = svc.get_characteristic(TX_UUID)
                r = svc.get_characteristic(RX_UUID)
                if t and r:
                    self._tx_char, self._rx_char = t, r
                    break
        if not (self._tx_char and self._rx_char):
            await client.disconnect()
            self._client = None
            raise RuntimeError("Kunne ikke finde NUS TX/RX i samme service.")

        await client.start_notify(self._tx_char, self._on_notify)

    def _on_notify(self, _handle: int, data: bytearray) -> None:
        self._rxbuf.extend(data)
        while True:
            try:
                idx = self._rxbuf.index(0x0A)  # '\n'
            except ValueError:
                break
            line = self._rxbuf[:idx]
            del self._rxbuf[:idx+1]
            txt = line.decode("utf-8", errors="ignore").strip()
            if not txt:
                continue

            # prøv JSON først
            delivered = False
            try:
                obj = json.loads(txt)
                # 1) json-callback
                if self._cb_json:
                    self._cb_json(obj)
                    delivered = True
                # 2) pair-callback (type/payload kompat)
                if self._cb_pair:
                    t = obj.get("type")
                    payload = obj.get("payload", obj if t is None else {})
                    self._cb_pair(t, payload)
                    delivered = True
            except Exception:
                pass

            # 3) raw fallback (ikke-JSON eller ingen callbacks ovenfor)
            if not delivered and self._cb_raw:
                self._cb_raw(txt)


# ---------- lille demo ----------
if __name__ == "__main__":

    async def demo():
        link = BleLink("BLE-LINK-TEST")

        # Lyt på JSON fra ESP32
        link.on_receive_json(lambda obj: print("[json ]", obj))

        # Lyt på rå tekst (fx "PONG")
        link.on_receive_raw(lambda s: print("[raw  ]", s))

        print("[demo] forbinder…")
        await link.connect()
        print("[demo] connected:", link.is_connected())

        # Python -> ESP32: send en JSON, som ESP32 echo'er tilbage
        print("[demo] send JSON echo")
        await link.send_json({"op": "echo", "msg": "hej fra Python"})

        # Python -> ESP32: send rå tekst, ESP32 svarer "PONG"
        print("[demo] send RAW PING")
        await link.send_raw("PING")

        # Vent mens vi modtager periodiske status-beskeder fra ESP32
        await asyncio.sleep(8)

        print("[demo] disconnect")
        await link.disconnect()
        print("[demo] done")

    asyncio.run(demo())
