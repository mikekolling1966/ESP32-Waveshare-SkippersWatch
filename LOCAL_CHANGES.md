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
Before deep sleep the QMI8658 IMU is put into low-power Wake-on-Motion (INT1 = GPIO21,
idles LOW, armed as an ext1 wake source next to the PWR button). On a motion wake,
`ImuShake::isDeliberateShake()` samples the accelerometer for up to 0.8 s before anything
else powers up: only two jolts beyond 1 g +- 0.8 g count as a shake. A shake shows the
Clock for 5 s with the radio off (touch -> normal wake); anything else (wrist turns,
boat motion) re-arms the IMU and sleeps again with the screen dark. Tested on USB:
gentle moves peaked at 0.15-0.97 g, firm shakes at 1.3-4.6 g.
- INT1 toggles on every motion event, so its level at sleep entry can be either value
  (the watch was just moved). The motion wake uses ext0 on GPIO21 with the opposite of
  the current level (button stays on ext1), so any change wakes it.
- IMU quirks found on this board: writes right after its oscillator restarts are dropped
  (all settings are written with read-back), and in WoM mode the data registers freeze
  (WoM is switched off before the shake check reads them).
- Status screen: the "ESP-NOW packets" row is replaced by "Shake wakes" (shown / all).
- Tuning: `SHAKE_JOLT_G`, `SHAKE_PEAKS`, `WOM_THRESHOLD_MG` in `ImuShake.h`.

## Building
```
cp secrets.h.example secrets.h   # then fill in WiFi, HA token, Funnel URL
pio run -t upload                 # PlatformIO, pioarduino platform 55.03.311 (Arduino-ESP32 3.3.11)
```
`secrets.h` is git-ignored. On USB the watch never sleeps; to flash a sleeping watch hold
BOOT while plugging in USB.
