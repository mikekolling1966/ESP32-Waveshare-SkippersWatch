#pragma once
#include <Arduino.h>
#include <math.h>

// Local addition (not upstream): values this boat has that the ESP-NOW protocol has no slot
// for — written by SignalKSource (core 0), read by BatteryUI / EngineUI (UI loop).
// Each value carries its own receive time; a page shows "---" once it is older than
// LocalValues::TIMEOUT_MS or NAN.
namespace LocalValues {
    enum Id : uint8_t { PORT_BATT_V, STBD_BATT_V, PORT_FUEL_L, STBD_FUEL_L, COUNT };
    constexpr uint32_t TIMEOUT_MS = 15000;

    struct Slot { float value = NAN; uint32_t rx_ms = 0; };
    inline Slot s_slots[COUNT];
    inline portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

    inline void set(Id id, float v) {
        portENTER_CRITICAL(&s_lock);
        s_slots[id].value = v;
        s_slots[id].rx_ms = millis();
        portEXIT_CRITICAL(&s_lock);
    }
    // NAN if never received, stale or unknown
    inline float get(Id id) {
        portENTER_CRITICAL(&s_lock);
        const Slot s = s_slots[id];
        portEXIT_CRITICAL(&s_lock);
        if (s.rx_ms == 0 || millis() - s.rx_ms > TIMEOUT_MS) return NAN;
        return s.value;
    }
}
