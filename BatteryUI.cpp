#include "BatteryUI.h"
#include "ui.h"
#include "LocalValues.h"

// === P U B L I C ===

BatteryUI::BatteryUI(ESPNowReceiver& receiver)
    : _receiver(receiver) {}

lv_obj_t* BatteryUI::getLvglScreen() const {
    return ui_ContainerBatteries;
}

void BatteryUI::begin() {
    if (_initialized) return;
    // Local change: this boat has two engine batteries (Shelly voltmeters) and no starter shunt
    // Layout: top = house bank (V, %), bottom = engine batteries (port, stbd)
    lv_label_set_text(ui_LabelStartVTitle, "House %");    // top right
    lv_label_set_text(ui_LabelHouseSOCTitle, "Port V");   // bottom left
    lv_label_set_text(ui_LabelHouseATitle, "Stbd V");     // bottom right
    this->showWaiting();
    _initialized = true;
}

void BatteryUI::onEnter() {
    this->showWaiting();
}

void BatteryUI::update() {
    if (!_initialized) return;
    char buf[16];

    // Engine batteries (LocalValues: own freshness), independent of the house frame
    const float port = LocalValues::get(LocalValues::PORT_BATT_V);
    const float stbd = LocalValues::get(LocalValues::STBD_BATT_V);
    if (isnan(port)) lv_label_set_text(ui_LabelHouseSOCValue, "---");
    else { snprintf(buf, sizeof(buf), "%.1f", port); lv_label_set_text(ui_LabelHouseSOCValue, buf); }
    if (isnan(stbd)) lv_label_set_text(ui_LabelHouseAValue, "---");
    else { snprintf(buf, sizeof(buf), "%.1f", stbd); lv_label_set_text(ui_LabelHouseAValue, buf); }

    // House bank (BATTERY_DELTA: SmartShunt via Home Assistant, or a VEDirect gateway)
    BatteryDelta b = _receiver.getBatteryData();
    uint32_t rx = _receiver.lastBatteryRxMillis();
    bool ok = (rx > 0) && ((millis() - rx) < BATTERY_TIMEOUT_MS);
    if (!ok || isnan(b.house_voltage)) lv_label_set_text(ui_LabelHouseVValue, "---");
    else { snprintf(buf, sizeof(buf), "%.1f", b.house_voltage); lv_label_set_text(ui_LabelHouseVValue, buf); }
    if (!ok || isnan(b.house_soc)) lv_label_set_text(ui_LabelStartVValue, "---");
    else { snprintf(buf, sizeof(buf), "%.0f", b.house_soc); lv_label_set_text(ui_LabelStartVValue, buf); }
}

// === P R I V A T E ===

void BatteryUI::showWaiting() {
    lv_label_set_text(ui_LabelHouseVValue, "---");
    lv_label_set_text(ui_LabelStartVValue, "---");
    lv_label_set_text(ui_LabelHouseSOCValue, "---");
    lv_label_set_text(ui_LabelHouseAValue, "---");
}
