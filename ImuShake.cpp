#include "ImuShake.h"

namespace {
    constexpr uint8_t ADDR = 0x6B;
    constexpr uint8_t REG_WHO_AM_I = 0x00, WHO_AM_I = 0x05;
    constexpr uint8_t REG_CTRL1 = 0x02, REG_CTRL2 = 0x03, REG_CTRL7 = 0x08, REG_CTRL9 = 0x0A;
    constexpr uint8_t REG_CAL1_L = 0x0B, REG_CAL1_H = 0x0C;
    constexpr uint8_t REG_STATUSINT = 0x2D, REG_STATUS0 = 0x2E, REG_STATUS1 = 0x2F, REG_AX_L = 0x35;
    constexpr int     DISCARD_SAMPLES = 4;      // first samples after a mode change are junk
    constexpr float   MAX_VALID_G = 7.9f;       // beyond the +-8 g range = a torn/garbage read
    constexpr uint8_t CTRL1_SENSOR_DISABLE = 0x01, CTRL1_INT1_EN = 0x08, CTRL1_ADDR_AI = 0x40;
    constexpr uint8_t CTRL2_8G_LP128 = 0x2C;   // +-8 g, 128 Hz low-power (WoM)
    constexpr uint8_t CTRL2_8G_125   = 0x26;   // +-8 g, 125 Hz normal (shake check)
    constexpr float   LSB_PER_G = 4096.0f;     // +-8 g
    constexpr uint8_t CMD_ACK = 0x00, CMD_WRITE_WOM = 0x08;

    RTC_DATA_ATTR uint32_t s_shown = 0, s_total = 0;
    float s_last_max_dev = 0;

    bool rd(TwoWire& w, uint8_t reg, uint8_t& v) {
        w.beginTransmission(ADDR); w.write(reg);
        if (w.endTransmission(false) != 0) return false;
        if (w.requestFrom(ADDR, (uint8_t)1) != 1) return false;
        v = w.read();
        return true;
    }
    bool wr(TwoWire& w, uint8_t reg, uint8_t v) {
        w.beginTransmission(ADDR); w.write(reg); w.write(v);
        return w.endTransmission() == 0;
    }
    // Write and read back; the IMU drops writes while its oscillator is starting
    bool wrv(TwoWire& w, uint8_t reg, uint8_t v) {
        for (int i = 0; i < 8; ++i) {
            uint8_t r = 0;
            if (wr(w, reg, v) && rd(w, reg, r) && r == v) return true;
            delay(5);
        }
        return false;
    }
    bool present(TwoWire& w) { uint8_t id = 0; return rd(w, REG_WHO_AM_I, id) && id == WHO_AM_I; }

    // Oscillator on, auto-increment, everything else as found; then let it settle
    bool oscillatorOn(TwoWire& w, uint8_t& ctrl1) {
        uint8_t c = 0;
        if (!rd(w, REG_CTRL1, c)) return false;
        ctrl1 = (uint8_t)((c & ~CTRL1_SENSOR_DISABLE) | CTRL1_ADDR_AI);
        wr(w, REG_CTRL1, ctrl1);
        delay(30);
        return wrv(w, REG_CTRL1, ctrl1);
    }
}

bool ImuShake::armWakeOnMotion(TwoWire& w) {
    if (!present(w)) return false;
    uint8_t ctrl1 = 0, st = 0;
    bool ok = oscillatorOn(w, ctrl1);
    ok = ok && wrv(w, REG_CTRL7, 0x00);                                   // sensors off while configuring
    ok = ok && wrv(w, REG_CTRL2, CTRL2_8G_LP128);
    ok = ok && wrv(w, REG_CAL1_L, WOM_THRESHOLD_MG);
    ok = ok && wrv(w, REG_CAL1_H, (uint8_t)(WOM_BLANKING & 0x3F));       // INT1, initial level LOW
    if (!ok) return false;
    wr(w, REG_CTRL9, CMD_WRITE_WOM);
    for (int i = 0; i < 25; ++i) { if (rd(w, REG_STATUSINT, st) && (st & 0x80)) break; delay(2); }
    wr(w, REG_CTRL9, CMD_ACK);
    rd(w, REG_STATUS1, st);                                               // clear any stale event
    ok = wrv(w, REG_CTRL7, 0x01);                                         // accelerometer on
    ok = ok && wrv(w, REG_CTRL1, (uint8_t)(ctrl1 | CTRL1_INT1_EN));
    return ok;
}

bool ImuShake::isDeliberateShake(TwoWire& w) {
    if (!present(w)) return true;   // cannot check -> do what the user asked (show the time)
    uint8_t ctrl1 = 0, st = 0;
    bool ok = oscillatorOn(w, ctrl1);
    ok = ok && wrv(w, REG_CTRL7, 0x00);
    // Leave Wake-on-Motion first (threshold 0 + WoM command): in WoM the data registers freeze
    ok = ok && wrv(w, REG_CAL1_L, 0) && wrv(w, REG_CAL1_H, 0);
    if (ok) {
        wr(w, REG_CTRL9, CMD_WRITE_WOM);
        for (int i = 0; i < 25; ++i) { if (rd(w, REG_STATUSINT, st) && (st & 0x80)) break; delay(2); }
        wr(w, REG_CTRL9, CMD_ACK);
    }
    ok = ok && wrv(w, REG_CTRL2, CTRL2_8G_125) && wrv(w, REG_CTRL7, 0x01);
    if (!ok) return true;
    delay(30);
    int peaks = 0, seen = 0;
    uint32_t last_peak = 0;
    s_last_max_dev = 0;
    const uint32_t t0 = millis();
    while (millis() - t0 < CHECK_WINDOW_MS) {
        uint8_t st0 = 0;
        if (!rd(w, REG_STATUS0, st0) || !(st0 & 0x01)) { delay(2); continue; }   // wait for a new sample
        uint8_t b[6];
        w.beginTransmission(ADDR); w.write(REG_AX_L);
        if (w.endTransmission(false) != 0 || w.requestFrom(ADDR, (uint8_t)6) != 6) return true;
        for (int i = 0; i < 6; ++i) b[i] = w.read();
        const float x = (int16_t)(b[0] | b[1] << 8) / LSB_PER_G;
        const float y = (int16_t)(b[2] | b[3] << 8) / LSB_PER_G;
        const float z = (int16_t)(b[4] | b[5] << 8) / LSB_PER_G;
        if (++seen <= DISCARD_SAMPLES) continue;
        const float mag = sqrtf(x * x + y * y + z * z);
        if (fabsf(x) > MAX_VALID_G || fabsf(y) > MAX_VALID_G || fabsf(z) > MAX_VALID_G) continue;
        const float dev = fabsf(mag - 1.0f);
        if (dev > s_last_max_dev) s_last_max_dev = dev;
        const uint32_t now = millis();
        if (dev > SHAKE_JOLT_G && (peaks == 0 || now - last_peak >= PEAK_GAP_MS)) {
            peaks++;
            last_peak = now;
            if (peaks >= SHAKE_PEAKS) return true;
        }
    }
    return false;
}

float ImuShake::lastMaxDeviationG() { return s_last_max_dev; }

void ImuShake::countShake(bool shown) { s_total++; if (shown) s_shown++; }
uint32_t ImuShake::shown() { return s_shown; }
uint32_t ImuShake::total() { return s_total; }
