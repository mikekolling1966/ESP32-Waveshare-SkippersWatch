#include "EngineUI.h"
#include "ui.h"
#include "LocalValues.h"

// === P U B L I C ===

EngineUI::EngineUI(ESPNowReceiver& receiver)
    : _receiver(receiver) {}

lv_obj_t* EngineUI::getLvglScreen() const {
    return ui_ContainerEngineroom;
}

void EngineUI::begin() {
    if (_initialized) return;
    // Local change: two fuel tanks on top (exhaust temperature not available on this boat)
    lv_label_set_text(ui_LabelExhaustCTitle, "Port L");
    lv_label_set_text(ui_LabelFuelLTitle, "Stbd L");
    this->showWaiting();
    _initialized = true;
}

void EngineUI::onEnter() {
    this->showWaiting();
}

void EngineUI::update() {
    if (!_initialized) return;

    const uint32_t now = millis();

    // --- Port / starboard fuel litres (LocalValues, from SignalK) ---
    {
        char buf[16];
        const float port = LocalValues::get(LocalValues::PORT_FUEL_L);
        const float stbd = LocalValues::get(LocalValues::STBD_FUEL_L);
        if (isnan(port)) lv_label_set_text(ui_LabelExhaustCValue, "---");
        else { snprintf(buf, sizeof(buf), "%.0f", port); lv_label_set_text(ui_LabelExhaustCValue, buf); }
        if (isnan(stbd)) lv_label_set_text(ui_LabelFuelLValue, "---");
        else { snprintf(buf, sizeof(buf), "%.0f", stbd); lv_label_set_text(ui_LabelFuelLValue, buf); }
    }

    // --- Fresh water (ratio 0..1 → liters) ---
    HALMETWaterDelta water = _receiver.getWaterData();
    uint32_t water_rx = _receiver.lastWaterRxMillis();
    bool water_ok = (water_rx > 0) && ((now - water_rx) < WATER_TIMEOUT_MS);
    if (water_ok) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f", water.water_level_ratio * WATER_TANK_CAPACITY_L);
        lv_label_set_text(ui_LabelFreshwaterLValue, buf);
    } else {
        lv_label_set_text(ui_LabelFreshwaterLValue, "---");
    }
}

// === P R I V A T E ===

void EngineUI::showWaiting() {
    lv_label_set_text(ui_LabelExhaustCValue, "---");
    lv_label_set_text(ui_LabelFuelLValue, "---");
    lv_label_set_text(ui_LabelFreshwaterLValue, "---");
}
