#include "TidesUI.h"
#include "ui.h"

namespace {
    lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_align_t align, int x, int y, const char* text) {
        lv_obj_t* l = lv_label_create(parent);
        lv_obj_set_style_text_font(l, font, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_align(l, align);
        lv_obj_set_pos(l, x, y);
        lv_label_set_text(l, text);
        return l;
    }
    constexpr int ROW_Y0 = -78;     // first row, from the container centre
    constexpr int ROW_STEP = 54;
}

void TidesUI::begin() {
    if (_initialized) return;
    _container = lv_obj_create(ui_ScreenMain);
    lv_obj_remove_style_all(_container);
    lv_obj_set_size(_container, 340, 440);
    lv_obj_set_align(_container, LV_ALIGN_CENTER);
    lv_obj_add_flag(_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(_container, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

    label(_container, &ui_font_FontLabel24, LV_ALIGN_TOP_MID, 0, 0, "Tides");
    label(_container, &ui_font_FontLabel14, LV_ALIGN_TOP_MID, 0, 34, "STROOD PIER");
    _date = label(_container, &ui_font_FontLabel24, LV_ALIGN_TOP_MID, 0, 62, "---");

    for (int i = 0; i < ROWS; ++i) {
        Row& r = _rows[i];
        r.box = lv_obj_create(_container);
        lv_obj_remove_style_all(r.box);
        lv_obj_set_size(r.box, 340, 48);
        lv_obj_set_align(r.box, LV_ALIGN_CENTER);
        lv_obj_set_y(r.box, ROW_Y0 + i * ROW_STEP);
        lv_obj_remove_flag(r.box, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        r.kind   = label(r.box, &ui_font_FontLabel24, LV_ALIGN_LEFT_MID, 0, 0, "");
        r.time   = label(r.box, &ui_font_FontLabel32, LV_ALIGN_CENTER, 6, 0, "");
        r.height = label(r.box, &ui_font_FontLabel24, LV_ALIGN_RIGHT_MID, 0, 0, "");
    }
    _footer = label(_container, &ui_font_FontLabel24, LV_ALIGN_BOTTOM_MID, 0, 0, "");
    this->showPlaceholders("waiting for tides");
    _initialized = true;
}

void TidesUI::onEnter() {
    _last_minute = -1;   // force a redraw
    this->update();
}

void TidesUI::update() {
    if (!_initialized) return;
    const time_t now = time(nullptr);
    const long minute = (long)(now / 60);
    if (minute == _last_minute && TideStore::s_version == _last_version) return;
    _last_minute = minute;
    _last_version = TideStore::s_version;
    this->render();
}

void TidesUI::showPlaceholders(const char* footer) {
    for (int i = 0; i < ROWS; ++i) {
        Row& r = _rows[i];
        bool show = i < 4;
        if (show) lv_obj_remove_flag(r.box, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(r.box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(r.box, LV_OPA_COVER, 0);
        lv_label_set_text(r.kind, "--");
        lv_label_set_text(r.time, "--:--");
        lv_label_set_text(r.height, "-.- m");
    }
    lv_label_set_text(_footer, footer);
}

void TidesUI::render() {
    const time_t now = time(nullptr);
    if (now < 1577836800) {   // clock not set yet: no "today" to show
        lv_label_set_text(_date, "---");
        this->showPlaceholders("waiting for clock");
        return;
    }
    struct tm lt;
    localtime_r(&now, &lt);
    static const char* const WD[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    char buf[32];
    snprintf(buf, sizeof(buf), "%s %02d.%02d", WD[lt.tm_wday], lt.tm_mday, lt.tm_mon + 1);
    lv_label_set_text(_date, buf);

    TideStore::Event ev[TideStore::MAX_EVENTS];
    const uint8_t n = TideStore::copy(ev, TideStore::MAX_EVENTS);
    if (n == 0) { this->showPlaceholders("waiting for tides"); return; }

    // Today's tides (local date) and the next tide from now (may be tomorrow)
    int shown = 0, next = -1;
    for (uint8_t i = 0; i < n; ++i) if (next < 0 && (time_t)ev[i].utc > now) next = i;
    for (uint8_t i = 0; i < n && shown < ROWS; ++i) {
        const time_t t = (time_t)ev[i].utc;
        struct tm et; localtime_r(&t, &et);
        if (et.tm_yday != lt.tm_yday || et.tm_year != lt.tm_year) continue;
        Row& r = _rows[shown++];
        lv_obj_remove_flag(r.box, LV_OBJ_FLAG_HIDDEN);
        snprintf(buf, sizeof(buf), "%s%s", (int)i == next ? ">" : "", ev[i].high ? "HW" : "LW");
        lv_label_set_text(r.kind, buf);
        snprintf(buf, sizeof(buf), "%02d:%02d", et.tm_hour, et.tm_min);
        lv_label_set_text(r.time, buf);
        snprintf(buf, sizeof(buf), "%.1f m", ev[i].height_cm / 100.0f);
        lv_label_set_text(r.height, buf);
        lv_obj_set_style_opa(r.box, t < now ? LV_OPA_40 : LV_OPA_COVER, 0);   // past tides dimmed
    }
    for (int i = shown; i < ROWS; ++i) lv_obj_add_flag(_rows[i].box, LV_OBJ_FLAG_HIDDEN);
    if (shown == 0) this->showPlaceholders("no tides for today");

    if (next >= 0) {
        const long mins = (long)(((time_t)ev[next].utc - now + 59) / 60);
        snprintf(buf, sizeof(buf), "Next %s in %ldh %02ldm", ev[next].high ? "HW" : "LW", mins / 60, mins % 60);
    } else {
        snprintf(buf, sizeof(buf), "tides out of date");
    }
    lv_label_set_text(_footer, buf);
}
