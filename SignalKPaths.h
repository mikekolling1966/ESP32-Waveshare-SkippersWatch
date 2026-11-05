#pragma once
// Local addition (not upstream): which SignalK paths feed which watch field.
// Each field lists candidate paths under vessels/self; the first one with a fresh value wins.
// Units are SignalK SI (rad, m/s, m, K, Pa, ratio 0..1, V, A) — SignalKSource converts.
// Edit to match your server (browse http://<server>:3000/signalk/v1/api/vessels/self).

namespace SignalKPaths {
    using List = const char* const[];

    // Navigation screen + Attitude screen
    constexpr List HEADING_TRUE = { "navigation.headingTrue", nullptr };
    constexpr List HEADING_MAG  = { "navigation.headingMagnetic", nullptr };
    constexpr List ATTITUDE     = { "navigation.attitude", nullptr };          // {roll, pitch, yaw}
    constexpr List POSITION     = { "navigation.position", nullptr };          // {latitude, longitude}
    constexpr List SOG          = { "navigation.speedOverGround", nullptr };
    constexpr List COG_TRUE     = { "navigation.courseOverGroundTrue", nullptr };
    constexpr List MAG_VAR      = { "navigation.magneticVariation", nullptr };
    constexpr List DEPTH_SURFACE    = { "environment.depth.belowSurface", nullptr };
    constexpr List DEPTH_TRANSDUCER = { "environment.depth.belowTransducer", nullptr };
    constexpr List DEPTH_KEEL       = { "environment.depth.belowKeel", nullptr };

    // Batteries screen: read from Home Assistant (not in SignalK on this boat), see HA_* below.

    // Weather screen
    constexpr List TEMPERATURE = { "environment.outside.temperature", "environment.cockpit.temperature", nullptr };
    constexpr List HUMIDITY    = { "environment.outside.relativeHumidity", "environment.outside.humidity",
                                   "environment.cockpit.relativeHumidity", nullptr };
    constexpr List PRESSURE    = { "environment.outside.pressure", nullptr };

    // Engine screen (this boat): top left = port fuel, top right = starboard fuel, bottom = fresh water.
    // Litres = currentLevel x capacity (SignalK capacity is m3).
    constexpr List PORT_FUEL_LEVEL    = { "tanks.fuel.port.currentLevel", nullptr };
    constexpr List PORT_FUEL_CAPACITY = { "tanks.fuel.port.capacity", nullptr };
    constexpr List STBD_FUEL_LEVEL    = { "tanks.fuel.starboard.currentLevel", nullptr };
    constexpr List STBD_FUEL_CAPACITY = { "tanks.fuel.starboard.capacity", nullptr };
    constexpr List WATER_LEVEL        = { "tanks.freshWater.0.currentLevel", nullptr };

    // Home Assistant entities (GET /api/states/<id>); "unknown"/"unavailable" shows "---"
    constexpr const char* HA_HOUSE_V   = "sensor.smartshunt_hq25464rt2m_voltage";   // Domestic 24 V SmartShunt
    constexpr const char* HA_HOUSE_A   = "sensor.smartshunt_hq25464rt2m_current";
    constexpr const char* HA_HOUSE_SOC = "sensor.smartshunt_hq25464rt2m_battery";   // %
    constexpr const char* HA_PORT_BATT_V = "sensor.shellyuni_08f9e0519036_adc";         // Shelly Uni, port engine
    constexpr const char* HA_STBD_BATT_V = "sensor.shellyplusuni_cc7b5c880224_voltmeter"; // Shelly Plus Uni, stbd engine

    // Remote mode only (no SignalK off the boat): the rest of the screens from Home Assistant.
    // "" = not available in HA (shows "---").
    constexpr const char* HA_TEMPERATURE = "sensor.h5179_e08e_temperature";            // cockpit Govee, °C
    constexpr const char* HA_HUMIDITY    = "sensor.h5179_e08e_humidity";               // cockpit Govee, %
    constexpr const char* HA_STBD_FUEL_L = "sensor.manxman_fuel_starboard_remaining";  // litres
    constexpr const char* HA_PORT_FUEL_L = "sensor.manxman_fuel_port_remaining";       // litres

    // Tides page (LAN and remote): attributes.events = [{time, height, event_type}, ...]
    constexpr const char* HA_TIDES = "sensor.strood_tides";

    // Max age of a value's SignalK timestamp before it counts as stale
    constexpr int NAV_MAX_AGE_S   = 10;   // heading, attitude, position, SOG/COG, depth
    constexpr int OTHER_MAX_AGE_S = 300;  // weather (sensors report ~1/min)
    constexpr int TANK_MAX_AGE_S  = 900;  // tank senders report every few minutes
}
