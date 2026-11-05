#pragma once
#include <Arduino.h>
#include "ESPNowReceiver.h"

// === C L A S S  S I G N A L K S O U R C E ===  (local addition, not upstream)
//
// - Reads the boat's SignalK server directly over WiFi instead of (or as well as) the
//   ESP-NOW gateways: a FreeRTOS task on core 0 joins WiFi (WifiTimeSync), sets the clock,
//   then polls GET /signalk/v1/api/vessels/self once a second.
// - Values are packed into the same wire structs the gateways broadcast and handed to
//   ESPNowReceiver::injectNetworkFrame(), so every page keeps its own freshness logic.
// - A value older than its max age (SignalK timestamp vs the system clock) is dropped,
//   so a switched-off instrument shows "---", never its last reading. Nothing is sent
//   until the clock is set (staleness cannot be judged before that).
// - Path mapping: SignalKPaths.h. Server address and WiFi: secrets.h.
// - Owned by: WatchApplication. begin() once per wake, after the ESP-NOW receiver is up.

class SignalKSource {
public:
    explicit SignalKSource(ESPNowReceiver& receiver) : _receiver(receiver) {}
    void begin();

private:
    static constexpr uint32_t POLL_INTERVAL_MS  = 1000;
    static constexpr uint32_t RETRY_INTERVAL_MS = 10000;
    static constexpr uint32_t TASK_STACK = 12288;
    static constexpr uint32_t HA_INTERVAL_MS = 5000;          // batteries change slowly
    static constexpr uint32_t HA_REMOTE_INTERVAL_MS = 5000;   // over Funnel (HTTPS, internet)
    static constexpr uint32_t HA_HOLD_MS = 20000;             // keep showing a reading this long

    ESPNowReceiver& _receiver;
    TaskHandle_t _task = nullptr;

    static void taskEntry(void* self);
    void run();
    bool pollOnce();
    void pollHomeAssistant();
    float haState(const char* entity_id);
    void resendCached();
    bool _remote = false;
    BatteryDelta _bat{};
    WeatherDelta _wx{};
    uint32_t _bat_ms = 0, _wx_ms = 0;
    uint32_t _last_ha_ms = 0;
    bool _ha_logged = false;
    bool _ha_error_logged = false;

    template <typename T>
    void send(ESPNowMsgType type, const T& payload);
};
