#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

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
    lv_obj_t *_pet_image = nullptr;
};

} // namespace esp_brookesia::apps
