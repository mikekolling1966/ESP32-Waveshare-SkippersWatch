#include "WifiTimeSync.h"
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <HTTPClient.h>
#include "secrets.h"

namespace {
    constexpr uint32_t FAST_CONNECT_TIMEOUT_MS = 5000;
    constexpr uint32_t CONNECT_TIMEOUT_MS      = 8000;
    constexpr uint32_t SNTP_TIMEOUT_MS         = 15000;  // IDF waits a random 0..5 s before the first request
    constexpr time_t   REFRESH_S               = 24 * 3600;
    constexpr size_t   NUM_NETWORKS = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);

    // RTC slow memory: survives deep sleep, cleared on power-on
    RTC_DATA_ATTR time_t  s_last_sync_utc = 0;
    RTC_DATA_ATTR int8_t  s_cached_net = -1;
    RTC_DATA_ATTR int32_t s_cached_channel = 0;
    RTC_DATA_ATTR uint8_t s_cached_bssid[6];

    volatile bool s_sntp_done = false;
    void onSntpSync(struct timeval*) { s_sntp_done = true; }

    bool waitConnected(uint32_t timeout_ms) {
        const uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeout_ms) delay(50);
        return WiFi.status() == WL_CONNECTED;
    }

    void remember(int idx) {
        s_cached_net = (int8_t)idx;
        s_cached_channel = WiFi.channel();
        memcpy(s_cached_bssid, WiFi.BSSID(), 6);
    }

    void logLink(const char* how, uint32_t t0) {
        Serial.printf("[wifi] %s %s in %lu ms, ch %d, RSSI %d, ip %s\n", how, WiFi.SSID().c_str(),
                      (unsigned long)(millis() - t0), WiFi.channel(), WiFi.RSSI(),
                      WiFi.localIP().toString().c_str());
    }

    // The SignalK server's HTTP Date header (RFC 7231, always GMT)
    bool syncFromHttpDate(const char* url) {
        HTTPClient http;
        http.setConnectTimeout(3000);
        http.setTimeout(3000);
        if (!http.begin(url)) return false;
        const char* keys[] = { "Date" };
        http.collectHeaders(keys, 1);
        const int code = http.GET();
        String date = http.header("Date");
        http.end();
        Serial.printf("[clock] HTTP %s -> %d, Date: %s\n", url, code, date.c_str());
        if (code <= 0 || date.length() < 25) return false;
        static const char* const MON[12] = { "Jan","Feb","Mar","Apr","May","Jun",
                                             "Jul","Aug","Sep","Oct","Nov","Dec" };
        char mon[4] = {0};
        int d, y, hh, mm, ss;
        if (sscanf(date.c_str() + 5, "%d %3s %d %d:%d:%d", &d, mon, &y, &hh, &mm, &ss) != 6) return false;
        int m = 0;
        for (int i = 0; i < 12; i++) if (!strcmp(mon, MON[i])) m = i + 1;
        if (m == 0 || y < 2020) return false;
        struct timeval tv = { WifiTimeSync::utcToEpoch(y, m, d, hh, mm, ss), 500000 };
        settimeofday(&tv, nullptr);
        return true;
    }
}

time_t WifiTimeSync::utcToEpoch(int y, int m, int d, int hh, int mm, int ss) {
    // days_from_civil (H. Hinnant), month 1..12
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long days = (long)era * 146097 + (long)doe - 719468;
    return (time_t)(days * 86400L + hh * 3600L + mm * 60L + ss);
}

time_t WifiTimeSync::parseIso8601(const char* s, float* frac_s) {
    if (!s) return 0;
    int y, mo, d, hh, mm; float sec;
    if (sscanf(s, "%d-%d-%dT%d:%d:%fZ", &y, &mo, &d, &hh, &mm, &sec) != 6) return 0;
    const int whole = (int)sec;
    if (frac_s) *frac_s = sec - whole;
    return utcToEpoch(y, mo, d, hh, mm, whole);
}

bool WifiTimeSync::currentIsRemote() {
    return s_cached_net >= 0 && s_cached_net < (int)NUM_NETWORKS && WIFI_NETWORKS[s_cached_net].remote;
}

bool WifiTimeSync::connect() {
    if (WiFi.status() == WL_CONNECTED) return true;
    WiFi.mode(WIFI_STA);
    const uint32_t t0 = millis();

    // Fast path: the network, channel and BSSID that worked last wake (no scan)
    if (s_cached_net >= 0 && s_cached_net < (int)NUM_NETWORKS) {
        const WifiCred& c = WIFI_NETWORKS[s_cached_net];
        WiFi.begin(c.ssid, c.password, s_cached_channel, s_cached_bssid);
        if (waitConnected(FAST_CONNECT_TIMEOUT_MS)) { logLink("rejoined", t0); return true; }
        WiFi.disconnect(false, true);
        s_cached_net = -1;
    }

    // Scan, then join the highest-priority known network in range
    const int n = WiFi.scanNetworks(false, false, false, 120);
    for (size_t k = 0; k < NUM_NETWORKS; k++) {
        const WifiCred& c = WIFI_NETWORKS[k];
        bool seen = false;
        for (int i = 0; i < n && !seen; i++) seen = (WiFi.SSID(i) == c.ssid);
        if (!seen) continue;
        WiFi.begin(c.ssid, c.password);
        if (waitConnected(CONNECT_TIMEOUT_MS)) {
            WiFi.scanDelete();
            remember((int)k);
            logLink("joined", t0);
            return true;
        }
        Serial.printf("[wifi] connect to %s failed (status %d)\n", c.ssid, (int)WiFi.status());
        WiFi.disconnect(false, true);
        delay(200);
    }
    Serial.printf("[wifi] no known network joined (%d networks seen)\n", n);
    WiFi.scanDelete();
    return false;
}

bool WifiTimeSync::syncClockIfNeeded() {
    const time_t now = time(nullptr);
    if (now > VALID_EPOCH && s_last_sync_utc && (now - s_last_sync_utc) < REFRESH_S) return false;
    if (WiFi.status() != WL_CONNECTED) return false;

    s_sntp_done = false;
    esp_sntp_stop();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    sntp_set_time_sync_notification_cb(onSntpSync);
    esp_sntp_init();
    const uint32_t t1 = millis();
    while (!s_sntp_done && millis() - t1 < SNTP_TIMEOUT_MS) delay(50);
    bool ok = s_sntp_done;
    esp_sntp_stop();

    const char* src = "NTP";
    if (!ok) {
        Serial.println("[clock] NTP timed out, trying SignalK server clock");
        ok = syncFromHttpDate(SIGNALK_BASE_URL "/signalk");
        src = "SignalK HTTP Date";
    }
    if (ok) {
        s_last_sync_utc = time(nullptr);
        struct tm ut; gmtime_r(&s_last_sync_utc, &ut);
        char buf[32]; strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &ut);
        Serial.printf("[clock] set from %s: %s\n", src, buf);
    } else {
        Serial.println("[clock] no time source reachable, clock unchanged");
    }
    return ok;
}
