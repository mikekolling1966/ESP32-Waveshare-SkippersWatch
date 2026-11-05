#pragma once
#include <Arduino.h>
#include <time.h>

// Local addition (not upstream): WiFi STA link + clock sync, used by SignalKSource.
// All calls run on the SignalKSource task (core 0), never the UI loop.
// - connect(): fast reconnect to the last network (cached in RTC memory across deep
//   sleep), else scan and join the highest-priority known network from secrets.h.
// - syncClockIfNeeded(): NTP, falling back to the SignalK server's HTTP Date header.
//   Runs on power-on / unset clock, then at most once per 24 h. Never touches TZ.
namespace WifiTimeSync {
    bool connect();
    // The joined network is marked remote in secrets.h (Home Assistant via Funnel)
    bool currentIsRemote();
    bool syncClockIfNeeded();

    // UTC civil date/time → Unix seconds (no TZ involvement). month is 1..12.
    time_t utcToEpoch(int year, int month, int day, int hh, int mm, int ss);
    // "2026-11-05T09:03:35.090Z" → Unix seconds (fraction in *frac_s if given); 0 on failure
    time_t parseIso8601(const char* s, float* frac_s = nullptr);
    // "2026-11-05T09:27:00+00:00" / "...Z" / "+01:00" -> Unix seconds; 0 on failure
    time_t parseIsoOffset(const char* s);

    constexpr time_t VALID_EPOCH = 1577836800;   // 2020-01-01: below this the clock is unset
}
