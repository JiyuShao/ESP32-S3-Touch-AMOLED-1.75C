#include "lvgl.h"
#include "esp_brookesia.hpp"
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

    // Create a placeholder pet image — a rounded rectangle with a label on it.
    // Full spritesheet rendering comes in ticket 03.
    lv_obj_t *screen = lv_screen_active();

    _pet_image = lv_obj_create(screen);
    lv_obj_set_size(_pet_image, 200, 220);
    lv_obj_align(_pet_image, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_radius(_pet_image, 20, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_pet_image, lv_color_hex(0x6080ff), LV_PART_MAIN);
    lv_obj_set_style_border_width(_pet_image, 3, LV_PART_MAIN);
    lv_obj_set_style_border_color(_pet_image, lv_color_hex(0x304080), LV_PART_MAIN);

    // Eyes
    lv_obj_t *eye_l = lv_obj_create(_pet_image);
    lv_obj_set_size(eye_l, 24, 24);
    lv_obj_set_pos(eye_l, 55, 65);
    lv_obj_set_style_radius(eye_l, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(eye_l, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(eye_l, 0, LV_PART_MAIN);

    lv_obj_t *pupil_l = lv_obj_create(eye_l);
    lv_obj_set_size(pupil_l, 10, 10);
    lv_obj_align(pupil_l, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(pupil_l, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pupil_l, lv_color_hex(0x202040), LV_PART_MAIN);
    lv_obj_set_style_border_width(pupil_l, 0, LV_PART_MAIN);

    lv_obj_t *eye_r = lv_obj_create(_pet_image);
    lv_obj_set_size(eye_r, 24, 24);
    lv_obj_set_pos(eye_r, 121, 65);
    lv_obj_set_style_radius(eye_r, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(eye_r, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(eye_r, 0, LV_PART_MAIN);

    lv_obj_t *pupil_r = lv_obj_create(eye_r);
    lv_obj_set_size(pupil_r, 10, 10);
    lv_obj_align(pupil_r, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(pupil_r, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(pupil_r, lv_color_hex(0x202040), LV_PART_MAIN);
    lv_obj_set_style_border_width(pupil_r, 0, LV_PART_MAIN);

    // Smile
    lv_obj_t *smile = lv_obj_create(_pet_image);
    lv_obj_set_size(smile, 60, 20);
    lv_obj_set_pos(smile, 70, 120);
    lv_obj_set_style_radius(smile, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_color(smile, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(smile, 0, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(smile, true, LV_PART_MAIN);

    // State label
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "AI Coding Pet");
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_text_color(label, lv_color_hex(0xc0c0ff), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);

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
    return true;
}

bool AiCodingPet::pause(void)
{
    ESP_UTILS_LOGD("Pause");
    return true;
}

bool AiCodingPet::resume(void)
{
    ESP_UTILS_LOGD("Resume");
    return true;
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, AiCodingPet, APP_NAME, []()
{
    return std::shared_ptr<AiCodingPet>(AiCodingPet::requestInstance(), [](AiCodingPet *p) {});
})

} // namespace esp_brookesia::apps
