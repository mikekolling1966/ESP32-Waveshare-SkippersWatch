#pragma once
#include <Arduino.h>
#include <time.h>

// Local addition: high/low water predictions for the Tides page (Rochester, Strood Pier).
// Kept in RTC slow memory, so a week of tides survives deep sleep and shows with no
// network (predictions do not go stale the way live readings do). Cleared on power-on.
// Written by SignalKSource (core 0), read by TidesUI (UI loop) under a spinlock.
namespace TideStore {
    struct Event { uint32_t utc; int16_t height_cm; uint8_t high; uint8_t pad; };
    constexpr size_t MAX_EVENTS = 40;
    constexpr uint32_t MAGIC = 0x54494445;   // 'TIDE'
    constexpr uint32_t REFRESH_S = 3 * 3600;   // refetch from Home Assistant after this

    struct Data { uint32_t magic; uint32_t fetched_utc; uint8_t count; Event ev[MAX_EVENTS]; };
    RTC_DATA_ATTR inline Data s_data;
    inline portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
    inline volatile uint32_t s_version = 0;   // bumps on every store, lets the page redraw

    inline bool valid() { return s_data.magic == MAGIC && s_data.count <= MAX_EVENTS; }
    inline bool needsRefresh(time_t now) {
        return !valid() || s_data.count == 0 || (uint32_t)now - s_data.fetched_utc > REFRESH_S;
    }
    inline void store(const Event* ev, uint8_t n, uint32_t now) {
        if (n > MAX_EVENTS) n = MAX_EVENTS;
        portENTER_CRITICAL(&s_lock);
        memcpy(s_data.ev, ev, n * sizeof(Event));
        s_data.count = n;
        s_data.fetched_utc = now;
        s_data.magic = MAGIC;
        s_version = s_version + 1;
        portEXIT_CRITICAL(&s_lock);
    }
    // Copy out; returns the number of events
    inline uint8_t copy(Event* out, size_t cap) {
        if (!valid()) return 0;
        portENTER_CRITICAL(&s_lock);
        uint8_t n = s_data.count < cap ? s_data.count : (uint8_t)cap;
        memcpy(out, s_data.ev, n * sizeof(Event));
        portEXIT_CRITICAL(&s_lock);
        return n;
    }
}
