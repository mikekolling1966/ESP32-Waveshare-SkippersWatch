# Local changes (MANXMAN build)

This branch adapts SkippersWatch v2.1.0 to a boat without the ESP-NOW gateway boxes:
the watch reads **SignalK** and **Home Assistant** directly over WiFi. The ESP-NOW
receiver is still running, so gateways can be added later. Code changes are marked
`Local addition` / `Local change` in the source.

## Data sources
- `SignalKSource` — FreeRTOS task on core 0: joins WiFi (`WifiTimeSync`), sets the clock
  (NTP, falling back to the SignalK server's HTTP `Date` header), then polls
  `GET /signalk/v1/api/vessels/self` once a second and Home Assistant every 5 s.
  Values are packed into the existing ESP-NOW wire structs and injected into
  `ESPNowReceiver`, so every page keeps its own freshness logic. Values older than their
  SignalK timestamp limit are dropped (10 s navigation, 5 min weather, 15 min tanks).
- **Remote mode** — on a network marked `remote` in `secrets.h` (e.g. a phone hotspot)
  the watch reads only Home Assistant, over HTTPS via Tailscale Funnel, verified against
  the Let's Encrypt roots in `LetsEncryptRoots.h`.
- `SignalKPaths.h` — which SignalK paths / HA entities feed which field. Edit this to remap.
- `LocalValues.h` — small store for values the ESP-NOW protocol has no slot for
  (port/starboard engine battery, port/starboard fuel litres).

## Screens
- **Batteries**: House V and House % (SmartShunt via HA) on top; Port V and Stbd V
  (engine batteries, Shelly via HA) on the bottom.
- **Engine**: Port L and Stbd L fuel on top, fresh water at the bottom (no exhaust sensor).
- Missing values show `---` instead of `nan` (NaN-safe converters in `espnow_protocol.h`).
- Clock page uses UK time (`LOCAL_TZ` in `WatchApplication.h`) instead of Finland.

## Tides page (page 9)
Today's high and low waters at Rochester (Strood Pier), UKHO station 0109B, in local time:
past tides dimmed, the next one marked `>`, and a "Next HW/LW in …" footer.
- Data: TidalPlan (UKHO Admiralty API) -> Home Assistant sensor `sensor.strood_tides`
  (package `homeassistant/strood_tides.yaml`, refreshed at startup and every 3 h) ->
  `SignalKSource::fetchTides()`, which works on the LAN and remotely via Funnel.
- `TideStore.h` keeps a week of tides in RTC memory, so the page works with no network
  and survives deep sleep (cleared on power-on, then refetched).
- `TidesUI` builds its container in code; the menu gets a ninth button in the free
  bottom-right slot (`ui_img_tides_png.c` / `ui_img_nighttides_png.c`, generated from
  `SLS_project_WS/assets/tides.png` / `nighttides.png`, not from SquareLine).

## Shake to show the time
Before deep sleep the QMI8658 IMU is put into Wake-on-Motion (INT1 = GPIO21, armed as an
ext1 wake source next to the PWR button). A shake wake shows the Clock for 5 s with the
radio off; touching the screen turns it into a normal wake. Sensitivity:
`WOM_THRESHOLD_MG` in `PmuRails.h`.

## Building
```
cp secrets.h.example secrets.h   # then fill in WiFi, HA token, Funnel URL
pio run -t upload                 # PlatformIO, pioarduino platform 55.03.311 (Arduino-ESP32 3.3.11)
```
`secrets.h` is git-ignored. On USB the watch never sleeps; to flash a sleeping watch hold
BOOT while plugging in USB.
