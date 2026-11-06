#pragma once
#include <Arduino.h>
#include <Wire.h>

// Local addition: QMI8658 shake handling (I2C 0x6B, INT1 = GPIO21).
//
// Hardware lesson (2026-11-05, measured on this board): register writes made right after
// the IMU's internal oscillator is (re)started are silently dropped, so every setting is
// written with read-back verification and retried. Without that the accelerometer never
// samples and Wake-on-Motion never fires.
//
// - armWakeOnMotion(): low-power WoM before deep sleep. INT1 idles LOW and toggles HIGH on
//   motion above WOM_THRESHOLD_MG (CAL1_H[7] = initial level, [6] = 0 selects INT1).
// - isDeliberateShake(): after a motion wake, sample the accelerometer for up to
//   CHECK_WINDOW_MS and require SHAKE_PEAKS jolts beyond SHAKE_JOLT_G from 1 g. Arm swings
//   and boat motion stay below that, so they go straight back to sleep with the panel dark.
// - Counters (RTC memory, cleared on power-on) for the Status screen.
namespace ImuShake {
    bool armWakeOnMotion(TwoWire& w);
    bool isDeliberateShake(TwoWire& w);
    void countShake(bool shown);
    // Safety net against wake loops: SUSPEND_AFTER ignored motion wakes within
    // SUSPEND_WINDOW_S switch motion wake off for SUSPEND_FOR_S (a button wake clears it).
    bool motionAllowed();
    void clearSuspend();
    bool suspended();
    uint32_t shown();
    uint32_t total();
    float lastMaxDeviationG();   // strongest jolt seen by the last check (for tuning/logs)

    constexpr uint8_t  WOM_THRESHOLD_MG = 250;   // wakes the CPU (cheap); the check decides
    constexpr uint8_t  WOM_BLANKING     = 16;    // samples ignored after arming (~0.1 s at 128 Hz)
    constexpr uint32_t CHECK_WINDOW_MS  = 800;
    constexpr float    SHAKE_JOLT_G     = 0.8f;  // |a| beyond 1 g +- this counts as a jolt
    constexpr int      SHAKE_PEAKS      = 2;
    constexpr uint32_t PEAK_GAP_MS      = 80;    // jolts closer than this are one jolt
    constexpr uint8_t  SUSPEND_AFTER    = 10;
    constexpr uint32_t SUSPEND_WINDOW_S = 120;
    constexpr uint32_t SUSPEND_FOR_S    = 900;
}
