#include "lvgl.h"
#include "esp_brookesia.hpp"
#include "pet_bridge_config.h"
#include "ws_client.h"
#include <cstring>
#include <cstdio>
#include <time.h>
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:ai-pet"
#include "esp_lib_utils.h"
#include "ai_coding_pet.hpp"
#include "pet_theme.h"

#define APP_NAME "AI Coding Pet"

using namespace std;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

LV_IMG_DECLARE(ai_coding_pet_icon_112_112);

namespace esp_brookesia::apps {

AiCodingPet *AiCodingPet::_instance = nullptr;

/* WS callbacks run on the ws_client task — they only feed PetBridge (no
 * LVGL calls); the LVGL timer below polls and updates the UI. */
void AiCodingPet::wsMessageCb(const char *payload, int len, void *user_data)
{
    AiCodingPet *self = static_cast<AiCodingPet *>(user_data);
    if (!self->_bridge.onWsMessage(payload, len, lv_tick_get())) {
        ESP_UTILS_LOGW("WS bad state message: %.*s", len, payload);
    }
}

void AiCodingPet::wsStatusCb(bool connected, void *user_data)
{
    AiCodingPet *self = static_cast<AiCodingPet *>(user_data);
    self->_bridge.onConnectionChanged(connected, lv_tick_get());
}

void AiCodingPet::tickTimerCb(lv_timer_t *t)
{
    AiCodingPet *self = static_cast<AiCodingPet *>(t->user_data);
    uint32_t now = lv_tick_get();
    self->_bridge.tick(now);
    self->_renderer.tick(now);
    self->updatePermOverlay();
    self->pollState();
    if (self->_screen == SCREEN_LIST) {
        self->refreshList(); // live session list while visible
    }
}

void AiCodingPet::pollState(void)
{
    AgentState state = _bridge.status().state;
    // the overlay is the top priority UX: play attention while it is up,
    // regardless of push ordering (bridge also forces attention on its side)
    if (_perm_overlay != nullptr && !lv_obj_has_flag(_perm_overlay, LV_OBJ_FLAG_HIDDEN)) {
        state = PET_STATE_ATTENTION;
    }
    if (_rendered_state != state) {
        _rendered_state = state;
        _renderer.playState(_rendered_state);
    }
}

AiCodingPet *AiCodingPet::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new AiCodingPet(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

AiCodingPet::AiCodingPet(bool use_status_bar, bool use_navigation_bar):
    App(APP_NAME, &ai_coding_pet_icon_112_112, true, use_status_bar, use_navigation_bar)
{
}

AiCodingPet::~AiCodingPet()
{
}

bool AiCodingPet::run(void)
{
    ESP_UTILS_LOGD("Run");

    /* WebSocket link to the PC Bridge (ticket 02). Auto-reconnects every 5 s. */
    if (!_ws_started) {
        ESP_UTILS_LOGI("Connecting WS: ws://%s:%d%s", PET_BRIDGE_IP, PET_BRIDGE_PORT, PET_BRIDGE_WS_PATH);
        if (ws_client_start(PET_BRIDGE_IP, PET_BRIDGE_PORT, PET_BRIDGE_WS_PATH,
                            wsMessageCb, wsStatusCb, this) != ESP_OK) {
            ESP_UTILS_LOGE("WS client start failed");
            return false;
        }
        _ws_started = true;
    }

    /* The shell leaves the app's visual area with its stylesheet bg (white in
     * practice); the pet app is dark by design — paint it explicitly and drop
     * the default scrollability (nothing on these pages scrolls). */
    lv_obj_set_style_bg_color(lv_screen_active(), pet_theme::current()->bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);

    /* Pet sprite + state label (ticket 03), driven by the bridge state. */
    _renderer.init(lv_screen_active());
    ESP_UTILS_LOGD("screen %dx%d scrollable=%d",
                   lv_obj_get_width(lv_screen_active()), lv_obj_get_height(lv_screen_active()),
                   lv_obj_has_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE));
    _rendered_state = PET_STATE_DISCONNECTED; // matches init visual

    /* Session list screen + screen dots + swipe (ticket 07). */
    createListUi(lv_screen_active());
    createPermOverlay(lv_screen_active()); // ticket 09: created last = topmost
    if (!_touch_cbs_added) {
        _touch_cbs_added = true;
        lv_obj_add_event_cb(lv_screen_active(), touchEventCb, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(lv_screen_active(), touchEventCb, LV_EVENT_RELEASED, this);
    }
    switchScreen(SCREEN_PET); // restore the pet screen on every launch

    if (!_intro_played) {
        _intro_played = true; // ticket 05: waving intro on first run only
        _renderer.playIntro();
    }

    if (_tick_timer == nullptr) {
        _tick_timer = lv_timer_create(tickTimerCb, 150, this);
    }

    return true;
}

bool AiCodingPet::back(void)
{
    ESP_UTILS_LOGD("Back");
    switchScreen(SCREEN_PET); // next launch starts on the pet screen
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool AiCodingPet::close(void)
{
    ESP_UTILS_LOGD("Close");
    if (_tick_timer != nullptr) {
        lv_timer_delete(_tick_timer);
        _tick_timer = nullptr;
    }
    _renderer.deinit(); // the shell keeps our screen; we own our LVGL objects
    destroyListUi();
    destroyPermOverlay();
    if (_touch_cbs_added) {
        _touch_cbs_added = false;
        lv_obj_remove_event_cb(lv_screen_active(), touchEventCb);
    }
    _screen = SCREEN_PET;
    if (_ws_started) {
        ws_client_stop();
        _ws_started = false;
    }
    return true;
}

bool AiCodingPet::pause(void)
{
    ESP_UTILS_LOGD("Pause");
    if (_tick_timer != nullptr) {
        lv_timer_pause(_tick_timer);
    }
    return true;
}

bool AiCodingPet::resume(void)
{
    ESP_UTILS_LOGD("Resume");
    if (_tick_timer != nullptr) {
        lv_timer_resume(_tick_timer);
    }
    pollState(); // catch up immediately instead of waiting for the next tick
    return true;
}

/* ---------------- ticket 07: pet screen ⇄ session list screen ---------------- */

namespace {
constexpr lv_coord_t SWIPE_DX = 60;
} // namespace

void AiCodingPet::createListUi(lv_obj_t *parent)
{
    _list_screen = lv_obj_create(parent);
    lv_obj_set_size(_list_screen, lv_pct(100), lv_pct(100));
    lv_obj_align(_list_screen, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(_list_screen, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_list_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_list_screen, 0, LV_PART_MAIN);
    // The shell's status bar floats over the app area: keep the list out
    // from under it (rows were hidden behind the bar at the very top).
    lv_obj_set_style_pad_top(_list_screen, 44, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(_list_screen, 16, LV_PART_MAIN);
    lv_obj_set_flex_flow(_list_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_list_screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(_list_screen, 14, LV_PART_MAIN);
    // Bubble touches up to the app screen — the swipe handler lives there
    // (LVGL only bubbles events from targets with this flag).
    lv_obj_add_flag(_list_screen, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(_list_screen, LV_OBJ_FLAG_HIDDEN);

    _list_header = lv_label_create(_list_screen);
    lv_obj_set_style_text_font(_list_header, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(_list_header, pet_theme::current()->fg, LV_PART_MAIN);

    _list_empty = lv_label_create(_list_screen);
    lv_label_set_text(_list_empty, "No active sessions");
    lv_obj_set_style_text_font(_list_empty, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(_list_empty, pet_theme::current()->comment, LV_PART_MAIN);

    for (int i = 0; i < PET_BRIDGE_MAX_SESSIONS; i++) {
        /* Card-style row: rounded panel + state-colored accent bar + text */
        lv_obj_t *panel = lv_obj_create(_list_screen);
        lv_obj_set_width(panel, lv_pct(92));
        lv_obj_set_height(panel, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(panel, lv_color_hex(0x33352D), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(panel, 10, LV_PART_MAIN);
        lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(panel, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_column(panel, 8, LV_PART_MAIN);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *accent = lv_obj_create(panel);
        lv_obj_set_width(accent, 4);
        lv_obj_set_height(accent, lv_pct(100));
        lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(accent, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(accent, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(accent, 0, LV_PART_MAIN);

        _list_rows[i].label = lv_label_create(panel);
        lv_obj_set_style_text_font(_list_rows[i].label, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(_list_rows[i].label, pet_theme::current()->comment, LV_PART_MAIN); // per-state color in refreshList
        lv_label_set_long_mode(_list_rows[i].label, LV_LABEL_LONG_WRAP); // long details wrap instead of clipping
        lv_obj_set_flex_grow(_list_rows[i].label, 1);
        _list_rows[i].panel = panel;
        _list_rows[i].accent = accent;
        _list_rows[i].cache[0] = '\0';
    }

    /* screen dots at the very bottom of the visual area */
    for (int i = 0; i < SCREEN_COUNT; i++) {
        _dots[i] = lv_obj_create(parent);
        lv_obj_set_size(_dots[i], 8, 8);
        lv_obj_align(_dots[i], LV_ALIGN_BOTTOM_MID, i == 0 ? -8 : 8, -8);
        lv_obj_set_style_radius(_dots[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(_dots[i], 0, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_dots[i], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_add_flag(_dots[i], LV_OBJ_FLAG_EVENT_BUBBLE);
    }
    updateDots();
}

void AiCodingPet::destroyListUi(void)
{
    if (_list_screen != nullptr) {
        lv_obj_del(_list_screen); // rows + header + empty die with it
        _list_screen = nullptr;
    }
    _list_header = nullptr;
    _list_empty = nullptr;
    for (int i = 0; i < PET_BRIDGE_MAX_SESSIONS; i++) {
        _list_rows[i].label = nullptr;
        _list_rows[i].panel = nullptr;
        _list_rows[i].accent = nullptr;
    }
    for (int i = 0; i < SCREEN_COUNT; i++) {
        if (_dots[i] != nullptr) {
            lv_obj_del(_dots[i]);
            _dots[i] = nullptr;
        }
    }
}

void AiCodingPet::switchScreen(Screen screen)
{
    if (_screen == screen) {
        return;
    }
    _screen = screen;
    _renderer.setVisible(screen == SCREEN_PET);
    if (screen == SCREEN_LIST) {
        refreshList();
        lv_obj_clear_flag(_list_screen, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_list_screen, LV_OBJ_FLAG_HIDDEN);
    }
    updateDots();
    ESP_UTILS_LOGI("screen: %s", screen == SCREEN_PET ? "pet" : "list");
}

void AiCodingPet::refreshList(void)
{
    if (_list_screen == nullptr) {
        return;
    }
    int n = 0;
    const pet_bridge::SessionEntry *sess = _bridge.sessions(&n);

    lv_label_set_text_fmt(_list_header, "%d active session%s", n, n == 1 ? "" : "s");
    if (n == 0) {
        if (!_empty_logged) {
            _empty_logged = true;
            ESP_UTILS_LOGI("list empty: no active sessions");
        }
        lv_obj_clear_flag(_list_empty, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < PET_BRIDGE_MAX_SESSIONS; i++) {
            lv_obj_add_flag(_list_rows[i].panel, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    _empty_logged = false;
    lv_obj_add_flag(_list_empty, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < PET_BRIDGE_MAX_SESSIONS; i++) {
        if (i >= n) {
            lv_obj_add_flag(_list_rows[i].panel, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const pet_bridge::SessionEntry &s = sess[i];
        // HH:MM is unambiguous here: bridge TTLs (30 min active / 5 min idle)
        // guarantee every row shown was updated within the last half hour.
        char text[sizeof(_list_rows[i].cache)];
        time_t t = (time_t)(s.updated_at_ms / 1000);
        struct tm ti;
        localtime_r(&t, &ti);
        // Card row: title line (state + project + time) and a detail line
        // with what the session is doing (prompt / tool + arg / hook event).
        const char *detail = s.detail[0] ? s.detail : s.event;
        snprintf(text, sizeof(text), "%s  %.18s  %02d:%02d\n  %.44s",
                 pet_render::stateName(s.state), s.basename[0] ? s.basename : "-",
                 ti.tm_hour, ti.tm_min,
                 detail[0] ? detail : "(no detail)");
        bool changed = (strcmp(text, _list_rows[i].cache) != 0);
        if (changed) {
            snprintf(_list_rows[i].cache, sizeof(_list_rows[i].cache), "%s", text);
            ESP_UTILS_LOGI("list row %d/%d: %s", i + 1, n, text);
            lv_label_set_text(_list_rows[i].label, text);
        }
        lv_obj_set_style_text_color(_list_rows[i].label,
                                    pet_theme::current()->state[s.state], LV_PART_MAIN);
        lv_obj_set_style_bg_color(_list_rows[i].accent,
                                  pet_theme::current()->state[s.state], LV_PART_MAIN);
        lv_obj_clear_flag(_list_rows[i].panel, LV_OBJ_FLAG_HIDDEN);
    }
}

void AiCodingPet::updateDots(void)
{
    for (int i = 0; i < SCREEN_COUNT; i++) {
        bool active = (_screen == (Screen)i);
        lv_obj_set_style_bg_color(_dots[i], active ? pet_theme::current()->dot_active
                                                  : pet_theme::current()->dot_inactive, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_dots[i], active ? LV_OPA_90 : LV_OPA_60, LV_PART_MAIN);
    }
}

/* ---------------- ticket 09: permission approval overlay ---------------- */

void AiCodingPet::createPermOverlay(lv_obj_t *parent)
{
    _perm_overlay = lv_obj_create(parent);
    lv_obj_set_size(_perm_overlay, lv_pct(100), lv_pct(100));
    lv_obj_align(_perm_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_perm_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_perm_overlay, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(_perm_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_perm_overlay, 0, LV_PART_MAIN);
    lv_obj_add_flag(_perm_overlay, LV_OBJ_FLAG_HIDDEN);
    // No EVENT_BUBBLE: while the overlay is up its zones swallow every press,
    // so the screen's swipe handler never sees them. That is the whole
    // point — page swipes are masked while a decision is pending (ticket 09).

    _perm_allow = lv_obj_create(_perm_overlay);
    lv_obj_set_size(_perm_allow, lv_pct(100), lv_pct(50));
    lv_obj_align(_perm_allow, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(_perm_allow, pet_theme::current()->state[PET_STATE_IDLE], LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_perm_allow, LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_border_width(_perm_allow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_perm_allow, 0, LV_PART_MAIN);
    lv_obj_add_flag(_perm_allow, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_perm_allow, permEventCb, LV_EVENT_CLICKED, this);

    _perm_deny = lv_obj_create(_perm_overlay);
    lv_obj_set_size(_perm_deny, lv_pct(100), lv_pct(50));
    lv_obj_align(_perm_deny, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(_perm_deny, pet_theme::current()->state[PET_STATE_ERROR], LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_perm_deny, LV_OPA_10, LV_PART_MAIN);
    lv_obj_set_style_border_width(_perm_deny, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_perm_deny, 0, LV_PART_MAIN);
    lv_obj_add_flag(_perm_deny, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_perm_deny, permEventCb, LV_EVENT_CLICKED, this);

    _perm_tool = lv_label_create(_perm_allow);
    lv_obj_set_style_text_font(_perm_tool, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(_perm_tool, pet_theme::current()->fg, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_perm_tool, 12, LV_PART_MAIN);
    lv_label_set_long_mode(_perm_tool, LV_LABEL_LONG_DOT);
    lv_obj_set_width(_perm_tool, lv_pct(100));
    lv_obj_align(_perm_tool, LV_ALIGN_TOP_MID, 0, 0);

    _perm_hint = lv_label_create(_perm_allow);
    lv_obj_set_style_text_font(_perm_hint, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(_perm_hint, pet_theme::current()->comment, LV_PART_MAIN);
    lv_label_set_long_mode(_perm_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_perm_hint, lv_pct(92));
    lv_obj_align(_perm_hint, LV_ALIGN_TOP_MID, 0, 46);

    lv_obj_t *allow_caption = lv_label_create(_perm_allow);
    lv_label_set_text(allow_caption, "Tap to allow (once)");
    lv_obj_set_style_text_font(allow_caption, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(allow_caption, pet_theme::current()->state[PET_STATE_IDLE], LV_PART_MAIN);
    lv_obj_align(allow_caption, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_obj_t *deny_caption = lv_label_create(_perm_deny);
    lv_label_set_text(deny_caption, "Tap to deny");
    lv_obj_set_style_text_font(deny_caption, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(deny_caption, pet_theme::current()->state[PET_STATE_ERROR], LV_PART_MAIN);
    lv_obj_align(deny_caption, LV_ALIGN_CENTER, 0, 0);
}

void AiCodingPet::destroyPermOverlay(void)
{
    if (_perm_overlay != nullptr) {
        lv_obj_del(_perm_overlay); // zones + labels die with it
    }
    _perm_overlay = nullptr;
    _perm_allow = nullptr;
    _perm_deny = nullptr;
    _perm_tool = nullptr;
    _perm_hint = nullptr;
}

void AiCodingPet::updatePermOverlay(void)
{
    if (_perm_overlay == nullptr) {
        return;
    }
    const pet_bridge::PermissionRequest &perm = _bridge.pendingPermission();
    bool pending = perm.permission_id[0] != '\0';
    bool shown = !lv_obj_has_flag(_perm_overlay, LV_OBJ_FLAG_HIDDEN);
    if (pending && !shown) {
        lv_obj_clear_flag(_perm_overlay, LV_OBJ_FLAG_HIDDEN);
    } else if (!pending && shown) {
        lv_obj_add_flag(_perm_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    if (pending && strcmp(perm.permission_id, _perm_shown_id) != 0) {
        snprintf(_perm_shown_id, sizeof(_perm_shown_id), "%s", perm.permission_id);
        lv_label_set_text(_perm_tool, perm.tool[0] ? perm.tool : "?");
        lv_label_set_text(_perm_hint, perm.hint);
        ESP_UTILS_LOGI("permission overlay: tool=%s hint=%.40s", perm.tool, perm.hint);
    }
}

void AiCodingPet::sendPermissionResponse(const char *decision)
{
    const pet_bridge::PermissionRequest &perm = _bridge.pendingPermission();
    if (perm.permission_id[0] == '\0') {
        return; // already resolved elsewhere
    }
    // spec: permission_response carries permission_id + decision; masked by
    // ws_client_send_text (≤125 bytes — a full UUID id fits)
    char msg[128];
    int n = snprintf(msg, sizeof(msg),
                     "{\"version\":\"v1\",\"type\":\"permission_response\","
                     "\"permission_id\":\"%s\",\"decision\":\"%s\"}",
                     perm.permission_id, decision);
    if (n <= 0 || n >= (int)sizeof(msg)) {
        ESP_UTILS_LOGE("permission response too long (%d)", n);
        return;
    }
    esp_err_t err = ws_client_send_text(msg);
    ESP_UTILS_LOGI("permission %s: tool=%s err=%s", decision, perm.tool, esp_err_to_name(err));
    if (err != ESP_OK) {
        return; // keep the overlay; the bridge's timeout settles ask (fail-open)
    }
    _bridge.clearPermission(); // overlay hides on the next tick
}

void AiCodingPet::permEventCb(lv_event_t *e)
{
    AiCodingPet *self = static_cast<AiCodingPet *>(lv_event_get_user_data(e));
    lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
    if (target == self->_perm_allow) {
        self->sendPermissionResponse("once"); // buddy vocabulary: once → allow
    } else if (target == self->_perm_deny) {
        self->sendPermissionResponse("deny");
    }
}

void AiCodingPet::touchEventCb(lv_event_t *e)
{
    AiCodingPet *self = static_cast<AiCodingPet *>(lv_event_get_user_data(e));
    lv_indev_t *indev = lv_indev_get_act();
    if (indev == nullptr) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        // Gesture recognition is disabled (shell nav gestures are inert on
        // this build), so every press on the screen is free for our swipe.
        self->_swipe_start_x = p.x;
        self->_swipe_start_y = p.y;
        return;
    }
    // RELEASED: hand-rolled swipe (LV_USE_GESTURE_RECOGNITION is off)
    lv_coord_t start_x = self->_swipe_start_x;
    lv_coord_t start_y = self->_swipe_start_y;
    self->_swipe_start_x = LV_COORD_MIN;
    self->_swipe_start_y = LV_COORD_MIN;
    if (start_x == LV_COORD_MIN) {
        return;
    }
    lv_coord_t dx = p.x - start_x;
    lv_coord_t dy = p.y - start_y;
    if (LV_ABS(dx) <= LV_ABS(dy)) {
        return; // not a horizontal swipe (ignore vertical scrolls and taps)
    }
    if (dx < -SWIPE_DX && self->_screen == SCREEN_PET) {
        self->switchScreen(SCREEN_LIST);
    } else if (dx > SWIPE_DX && self->_screen == SCREEN_LIST) {
        self->switchScreen(SCREEN_PET);
    }
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, AiCodingPet, APP_NAME, []()
{
    return std::shared_ptr<AiCodingPet>(AiCodingPet::requestInstance(), [](AiCodingPet *p) {});
})

} // namespace esp_brookesia::apps
