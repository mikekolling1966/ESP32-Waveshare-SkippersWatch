#pragma once
#include <Arduino.h>
#include <lvgl.h>
#include "IScreenUI.h"
#include "TideStore.h"

// === C L A S S  T I D E S U I ===  (local addition, not upstream)
//
// - The ninth page: today's high and low waters at Rochester (Strood Pier), local time,
//   from TideStore (UKHO predictions via TidalPlan -> Home Assistant -> SignalKSource).
// - Built in code (not SquareLine): a 340x440 container on ui_ScreenMain like the other
//   pages; labels inherit the theme's text color, so night mode works unchanged.
// - Rows: "HW/LW  time  height"; past tides dimmed, the next one marked with ">".
//   Footer: time until the next tide (which may be tomorrow's first).
// - Redraws when the minute changes or new tides arrive. No clock -> placeholders.

class TidesUI : public IScreenUI {
public:
    void begin() override;
    lv_obj_t* getLvglScreen() const override { return _container; }
    void onEnter() override;
    void update() override;

private:
    static constexpr int ROWS = 5;   // a day has 3-5 high/low waters
    lv_obj_t* _container = nullptr;
    lv_obj_t* _date = nullptr;
    lv_obj_t* _footer = nullptr;
    struct Row { lv_obj_t* box; lv_obj_t* kind; lv_obj_t* time; lv_obj_t* height; };
    Row _rows[ROWS] = {};
    long _last_minute = -1;
    uint32_t _last_version = 0xFFFFFFFF;
    bool _initialized = false;

    void render();
    void showPlaceholders(const char* footer);
};
