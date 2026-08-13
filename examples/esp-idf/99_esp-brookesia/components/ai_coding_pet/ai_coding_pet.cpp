#include "lvgl.h"
#include "esp_brookesia.hpp"
#include "pet_bridge_config.h"
#include "ws_client.h"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:ai-pet"
#include "esp_lib_utils.h"
#include "ai_coding_pet.hpp"

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
    self->pollState();
}

void AiCodingPet::pollState(void)
{
    if (_rendered_state != _bridge.status().state) {
        _rendered_state = _bridge.status().state;
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

    /* Pet sprite + state label (ticket 03), driven by the bridge state. */
    _renderer.init(lv_screen_active());
    _rendered_state = PET_STATE_DISCONNECTED; // matches init visual

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

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, AiCodingPet, APP_NAME, []()
{
    return std::shared_ptr<AiCodingPet>(AiCodingPet::requestInstance(), [](AiCodingPet *p) {});
})

} // namespace esp_brookesia::apps
