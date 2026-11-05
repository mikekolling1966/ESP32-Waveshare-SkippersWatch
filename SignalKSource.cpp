#include "SignalKSource.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "WifiTimeSync.h"
#include <WiFiClientSecure.h>
#include "LetsEncryptRoots.h"
#include "SignalKPaths.h"
#include "secrets.h"
#include "LocalValues.h"
#include "TideStore.h"

namespace {
    constexpr const char* URL = SIGNALK_BASE_URL "/signalk/v1/api/vessels/self";

    // Walk "a.b.c" from the vessel root to a SignalK leaf ({value, timestamp, ...})
    JsonVariantConst leaf(JsonObjectConst root, const char* path) {
        JsonVariantConst node = root;
        char key[48];
        const char* p = path;
        while (*p) {
            const char* dot = strchr(p, '.');
            size_t len = dot ? (size_t)(dot - p) : strlen(p);
            if (len >= sizeof(key)) return JsonVariantConst();
            memcpy(key, p, len); key[len] = 0;
            node = node[key];
            if (node.isNull()) return node;
            p += len + (dot ? 1 : 0);
        }
        return node;
    }

    // Age of a leaf's timestamp in ms (relative to the system clock), or -1 if unknown
    int32_t ageMs(JsonVariantConst lf, time_t now_s, uint32_t now_frac_ms) {
        float frac = 0;
        const time_t ts = WifiTimeSync::parseIso8601(lf["timestamp"].as<const char*>(), &frac);
        if (ts == 0) return -1;
        int64_t age = (int64_t)(now_s - ts) * 1000 + (int64_t)now_frac_ms - (int64_t)(frac * 1000);
        if (age < 0) age = 0;   // server slightly ahead of the watch
        return (int32_t)(age > INT32_MAX ? INT32_MAX : age);
    }

    struct Ctx {
        JsonObjectConst root;
        time_t now_s;
        uint32_t now_frac_ms;
    };

    // First candidate with a fresh value; returns its "value" node (null if none).
    JsonVariantConst fresh(const Ctx& c, const char* const* list, int max_age_s, int32_t* age_out = nullptr) {
        for (; *list; ++list) {
            JsonVariantConst lf = leaf(c.root, *list);
            if (lf.isNull() || lf["value"].isNull()) continue;
            const int32_t age = ageMs(lf, c.now_s, c.now_frac_ms);
            if (age < 0 || age > max_age_s * 1000) continue;
            if (age_out) *age_out = age;
            return lf["value"];
        }
        return JsonVariantConst();
    }

    float num(JsonVariantConst v) { return v.is<float>() ? v.as<float>() : NAN; }
}

void SignalKSource::begin() {
    if (_task) return;
    xTaskCreatePinnedToCore(taskEntry, "signalk", TASK_STACK, this, 1, &_task, 0);
}

void SignalKSource::taskEntry(void* self) {
    static_cast<SignalKSource*>(self)->run();
}

void SignalKSource::run() {
    uint32_t last_fail_log = 0;
    for (;;) {
        if (!WifiTimeSync::connect()) { delay(RETRY_INTERVAL_MS); continue; }
        WifiTimeSync::syncClockIfNeeded();
        const bool remote = WifiTimeSync::currentIsRemote();
        if (remote != _remote) {
            _remote = remote;
            _ha_logged = false; _ha_error_logged = false;
            Serial.printf("[signalk] mode: %s\n", remote ? "remote (Home Assistant via Funnel)" : "boat LAN (SignalK + Home Assistant)");
        }
        const uint32_t t0 = millis();
        bool ok = true;
        if (!remote) ok = pollOnce();    // SignalK is only reachable on the boat / home LAN
        if (!ok && millis() - last_fail_log > 10000) {
            last_fail_log = millis();
            Serial.println("[signalk] poll failed");
        }
        const uint32_t ha_every = remote ? HA_REMOTE_INTERVAL_MS : HA_INTERVAL_MS;
        if (_last_ha_ms == 0 || t0 - _last_ha_ms >= ha_every) { _last_ha_ms = t0; pollHomeAssistant(); }
        resendCached();
        fetchTides();
        const uint32_t spent = millis() - t0;
        delay(spent < POLL_INTERVAL_MS ? POLL_INTERVAL_MS - spent : 10);
    }
}

// Home Assistant: one GET per entity (keep-alive), state parsed as a number; NAN if
// unknown/unavailable or the request failed.
// GET /api/states/<entity> from Home Assistant (LAN, or HTTPS via Funnel in remote mode)
bool SignalKSource::haGet(const char* entity_id, String& body) {
    if (!entity_id || !entity_id[0]) return false;
    static HTTPClient http;
    static WiFiClientSecure tls;
    static bool tls_ready = false;
    http.setReuse(true);
    http.setConnectTimeout(_remote ? 5000 : 2000);
    http.setTimeout(_remote ? 5000 : 2000);
    String url = String(_remote ? HA_REMOTE_URL : HA_BASE_URL) + "/api/states/" + entity_id;
    bool begun;
    if (_remote) {
        if (!tls_ready) { tls.setCACert(LETSENCRYPT_ROOTS_PEM); tls_ready = true; }
        begun = http.begin(tls, url);
    } else {
        begun = http.begin(url);
    }
    if (!begun) return false;
    http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
    const int code = http.GET();
    if (code != 200) {
        if (!_ha_error_logged) { Serial.printf("[ha] %s -> HTTP %d\n", entity_id, code); _ha_error_logged = true; }
        http.end();
        return false;
    }
    body = http.getString();
    http.end();
    return true;
}

// Home Assistant: state parsed as a number; NAN if unknown/unavailable or the request failed.
float SignalKSource::haState(const char* entity_id) {
    String body;
    if (!haGet(entity_id, body)) return NAN;
    JsonDocument doc;
    JsonDocument filter; filter["state"] = true;
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return NAN;
    const char* st = doc["state"] | "";
    char* end = nullptr;
    const float v = strtof(st, &end);
    return (end && end != st && *end == 0) ? v : NAN;
}

// Tides: refetch the week of Strood Pier high/low waters when the RTC cache is empty/old
void SignalKSource::fetchTides() {
    if (HA_TOKEN[0] == 0) return;
    const time_t now = time(nullptr);
    if (now < WifiTimeSync::VALID_EPOCH || !TideStore::needsRefresh(now)) return;
    if (_last_tide_try_ms && millis() - _last_tide_try_ms < 60000) return;   // retry at most once a minute
    _last_tide_try_ms = millis();
    String body;
    if (!haGet(SignalKPaths::HA_TIDES, body)) return;
    JsonDocument filter; filter["attributes"]["events"][0]["time"] = true;
    filter["attributes"]["events"][0]["height"] = true;
    filter["attributes"]["events"][0]["event_type"] = true;
    JsonDocument doc;
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return;
    TideStore::Event ev[TideStore::MAX_EVENTS];
    uint8_t n = 0;
    for (JsonObjectConst e : doc["attributes"]["events"].as<JsonArrayConst>()) {
        if (n >= TideStore::MAX_EVENTS) break;
        const time_t t = WifiTimeSync::parseIsoOffset(e["time"].as<const char*>());
        if (t == 0) continue;
        const char* type = e["event_type"] | "";
        ev[n].utc = (uint32_t)t;
        ev[n].height_cm = (int16_t)lroundf((e["height"] | 0.0f) * 100.0f);
        ev[n].high = (strstr(type, "High") != nullptr);
        ev[n].pad = 0;
        n++;
    }
    if (n == 0) return;
    TideStore::store(ev, n, (uint32_t)now);
    Serial.printf("[tides] %u events cached (Strood Pier)\n", n);
}

void SignalKSource::pollHomeAssistant() {
    if (HA_TOKEN[0] == 0) return;
    using namespace SignalKPaths;
    BatteryDelta b;
    b.house_voltage = haState(HA_HOUSE_V);
    b.house_current = haState(HA_HOUSE_A);
    b.house_soc     = haState(HA_HOUSE_SOC);
    b.house_power   = (isnan(b.house_voltage) || isnan(b.house_current)) ? NAN : b.house_voltage * b.house_current;
    b.start_voltage = NAN;   // engine batteries are shown from LocalValues instead
    if (!isnan(b.house_voltage) || !isnan(b.house_soc)) { _bat = b; _bat_ms = millis(); }
    const float port = haState(HA_PORT_BATT_V);
    const float stbd = haState(HA_STBD_BATT_V);
    if (!isnan(port)) LocalValues::set(LocalValues::PORT_BATT_V, port);
    if (!isnan(stbd)) LocalValues::set(LocalValues::STBD_BATT_V, stbd);

    float t = NAN, h = NAN, pf = NAN, sf = NAN;
    if (_remote) {   // off the boat LAN, Home Assistant stands in for SignalK
        t  = haState(HA_TEMPERATURE);
        h  = haState(HA_HUMIDITY);
        sf = haState(HA_STBD_FUEL_L);
        pf = haState(HA_PORT_FUEL_L);
        if (!isnan(t) || !isnan(h)) {
            WeatherDelta w; w.temperature_c = t; w.humidity_p = h; w.pressure_hpa = NAN;
            _wx = w; _wx_ms = millis();
        }
        if (!isnan(pf)) LocalValues::set(LocalValues::PORT_FUEL_L, pf);
        if (!isnan(sf)) LocalValues::set(LocalValues::STBD_FUEL_L, sf);
    }
    resendCached();
    if (!_ha_logged) {
        _ha_logged = true;
        Serial.printf("[ha] %s: house %.2f V %.1f A %.0f %%, port %.2f V, stbd %.2f V",
                      _remote ? "remote" : "lan", b.house_voltage, b.house_current, b.house_soc, port, stbd);
        if (_remote) Serial.printf(", %.1f C %.0f %%, fuel port %.0f L stbd %.0f L", t, h, pf, sf);
        Serial.println();
    }
}

// Re-send the last good Home Assistant frames between polls so the pages' 6 s freshness
// windows never lapse on a slow round trip; dropped once older than HA_HOLD_MS.
void SignalKSource::resendCached() {
    const uint32_t now = millis();
    if (_bat_ms && now - _bat_ms < HA_HOLD_MS) this->send(ESPNowMsgType::BATTERY_DELTA, _bat);
    if (_remote && _wx_ms && now - _wx_ms < HA_HOLD_MS) this->send(ESPNowMsgType::WEATHER_DELTA, _wx);
}

bool SignalKSource::pollOnce() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    if (tv.tv_sec < WifiTimeSync::VALID_EPOCH) return false;   // cannot judge staleness yet

    static HTTPClient http;   // kept across polls for keep-alive
    http.setReuse(true);
    http.setConnectTimeout(2000);
    http.setTimeout(2000);
    if (!http.begin(URL)) return false;
    const int code = http.GET();
    if (code != 200) { http.end(); return false; }
    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) return false;

    gettimeofday(&tv, nullptr);
    const Ctx c{ doc.as<JsonObjectConst>(), tv.tv_sec, (uint32_t)(tv.tv_usec / 1000) };
    using namespace SignalKPaths;
    static bool s_logged_first = false;
    char seen[96] = "";

    // --- Heading + attitude (one frame; NAN fields show "---") ---
    {
        const float hdg_t = num(fresh(c, HEADING_TRUE, NAV_MAX_AGE_S));
        float hdg_m       = num(fresh(c, HEADING_MAG,  NAV_MAX_AGE_S));
        JsonVariantConst att = fresh(c, ATTITUDE, NAV_MAX_AGE_S);
        const float pitch = att.isNull() ? NAN : num(att["pitch"]);
        const float roll  = att.isNull() ? NAN : num(att["roll"]);
        if (!isnan(hdg_t) || !isnan(pitch) || !isnan(roll)) {
            HeadingDelta h;
            h.heading_true_rad = hdg_t;
            h.heading_rad = hdg_m;
            h.pitch_rad = pitch;
            h.roll_rad = roll;
            this->send(ESPNowMsgType::HEADING_DELTA, h);
            strcat(seen, " hdg/att");
        }
    }

    // --- GNSS ---
    {
        JsonVariantConst pos = fresh(c, POSITION, NAV_MAX_AGE_S);
        const float lat = pos.isNull() ? NAN : num(pos["latitude"]);
        const float lon = pos.isNull() ? NAN : num(pos["longitude"]);
        if (!isnan(lat) && !isnan(lon)) {
            GnssDelta g;
            g.lat_deg = lat;
            g.lon_deg = lon;
            g.sog_ms = num(fresh(c, SOG, NAV_MAX_AGE_S));
            g.cog_true_rad = num(fresh(c, COG_TRUE, NAV_MAX_AGE_S));
            g.mag_var_rad = num(fresh(c, MAG_VAR, 3600));
            g.satellites = 0;
            g.fix_type = 3;
            g.fix_ok = 1;
            g.reserved = 0;
            this->send(ESPNowMsgType::GNSS_DELTA, g);
            strcat(seen, " gnss");
        }
    }

    // --- Depth ---
    {
        int32_t age = 0;
        const float surf = num(fresh(c, DEPTH_SURFACE, NAV_MAX_AGE_S, &age));
        const float xdcr = num(fresh(c, DEPTH_TRANSDUCER, NAV_MAX_AGE_S));
        const float keel = num(fresh(c, DEPTH_KEEL, NAV_MAX_AGE_S));
        if (!isnan(surf) || !isnan(xdcr) || !isnan(keel)) {
            DepthDelta d;
            d.below_surface_m = surf;
            d.below_transducer_m = xdcr;
            d.below_keel_m = keel;
            d.age_ms = isnan(surf) ? UINT32_MAX : (uint32_t)age;
            this->send(ESPNowMsgType::DEPTH_DELTA, d);
            strcat(seen, " depth");
        }
    }

    // --- Weather ---
    {
        WeatherDelta w;
        const float t = num(fresh(c, TEMPERATURE, OTHER_MAX_AGE_S));
        const float h = num(fresh(c, HUMIDITY, OTHER_MAX_AGE_S));
        const float p = num(fresh(c, PRESSURE, OTHER_MAX_AGE_S));
        w.temperature_c = isnan(t) ? NAN : t - 273.15f;
        w.humidity_p = isnan(h) ? NAN : h * 100.0f;
        w.pressure_hpa = isnan(p) ? NAN : p / 100.0f;
        if (!isnan(t) || !isnan(h) || !isnan(p)) {
            this->send(ESPNowMsgType::WEATHER_DELTA, w);
            strcat(seen, " weather");
        }
    }

    // --- Tanks: port / starboard fuel (litres) to LocalValues, fresh water via the receiver ---
    {
        auto litres = [&](const char* const* level, const char* const* cap) {
            const float l = num(fresh(c, level, TANK_MAX_AGE_S));
            const float m3 = num(fresh(c, cap, 365 * 86400));   // capacity is static metadata
            return (isnan(l) || isnan(m3)) ? NAN : l * m3 * 1000.0f;
        };
        const float port = litres(PORT_FUEL_LEVEL, PORT_FUEL_CAPACITY);
        const float stbd = litres(STBD_FUEL_LEVEL, STBD_FUEL_CAPACITY);
        if (!isnan(port)) { LocalValues::set(LocalValues::PORT_FUEL_L, port); strcat(seen, " fuel-port"); }
        if (!isnan(stbd)) { LocalValues::set(LocalValues::STBD_FUEL_L, stbd); strcat(seen, " fuel-stbd"); }
        const float wa = num(fresh(c, WATER_LEVEL, TANK_MAX_AGE_S));
        if (!isnan(wa)) { HALMETWaterDelta t; t.water_level_ratio = wa; this->send(ESPNowMsgType::HALMET_WATER_DELTA, t); strcat(seen, " water"); }
    }

    if (!s_logged_first) {
        s_logged_first = true;
        Serial.printf("[signalk] first poll (%u bytes), fresh:%s\n", (unsigned)body.length(), seen[0] ? seen : " none");
    }
    return true;
}

template <typename T>
void SignalKSource::send(ESPNowMsgType type, const T& payload) {
    ESPNowPacket<T> pkt;
    initHeader(pkt.hdr, type, sizeof(T));
    pkt.payload = payload;
    _receiver.injectNetworkFrame(reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
}
