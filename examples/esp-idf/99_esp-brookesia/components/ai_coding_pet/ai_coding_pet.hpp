#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "pet_bridge.h"
#include "pet_render.h"

namespace esp_brookesia::apps {

class AiCodingPet: public systems::phone::App {
public:
    static AiCodingPet *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~AiCodingPet();

protected:
    AiCodingPet(bool use_status_bar, bool use_navigation_bar);

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    static AiCodingPet *_instance;
    pet_bridge::PetBridge _bridge;
    pet_render::PetRenderer _renderer;
    AgentState _rendered_state = PET_STATE_DISCONNECTED;
    lv_timer_t *_tick_timer = nullptr;
    bool _ws_started = false;
    bool _intro_played = false; // waving intro plays once per boot
    bool _touch_cbs_added = false;

    /* ticket 07: pet screen ⇄ session list screen */
    enum Screen { SCREEN_PET = 0, SCREEN_LIST, SCREEN_COUNT };
    Screen _screen = SCREEN_PET;
    struct Row {
        lv_obj_t *label;
        char cache[96];
    };
    lv_obj_t *_list_screen = nullptr;
    lv_obj_t *_list_header = nullptr;
    lv_obj_t *_list_empty = nullptr;
    Row _list_rows[PET_BRIDGE_MAX_SESSIONS] = {};
    lv_obj_t *_dots[SCREEN_COUNT] = {};
    lv_coord_t _swipe_start_x = LV_COORD_MIN;
    lv_coord_t _swipe_start_y = LV_COORD_MIN;
    bool _empty_logged = false;

    /* ticket 09: permission approval overlay (top half = allow, bottom = deny) */
    lv_obj_t *_perm_overlay = nullptr;
    lv_obj_t *_perm_allow = nullptr;
    lv_obj_t *_perm_deny = nullptr;
    lv_obj_t *_perm_tool = nullptr;
    lv_obj_t *_perm_hint = nullptr;
    char _perm_shown_id[sizeof(pet_bridge::PermissionRequest::permission_id)] = {}; // id the overlay texts were set for

    /** Push bridge state into the renderer on change. */
    void pollState(void);
    void createListUi(lv_obj_t *parent);
    void destroyListUi(void);
    void switchScreen(Screen screen);
    void refreshList(void);
    void updateDots(void);
    void createPermOverlay(lv_obj_t *parent);
    void destroyPermOverlay(void);
    void updatePermOverlay(void);
    void sendPermissionResponse(const char *decision);

    /* WS callbacks run on the ws_client task — they only feed PetBridge
     * (no LVGL calls); the LVGL timer polls and updates the UI. */
    static void wsMessageCb(const char *payload, int len, void *user_data);
    static void wsStatusCb(bool connected, void *user_data);
    static void tickTimerCb(lv_timer_t *t);
    static void touchEventCb(lv_event_t *e);
    static void permEventCb(lv_event_t *e);
};

} // namespace esp_brookesia::apps
