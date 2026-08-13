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

    /* WS callbacks run on the ws_client task — they only feed PetBridge
     * (no LVGL calls); the LVGL timer polls and updates the UI. */
    static void wsMessageCb(const char *payload, int len, void *user_data);
    static void wsStatusCb(bool connected, void *user_data);
    static void tickTimerCb(lv_timer_t *t);
};

} // namespace esp_brookesia::apps
