#pragma once

#include "pet_bridge.h"
#include "lvgl.h"

namespace pet_render {

/**
 * Plays the sprite rows generated from the Codex-pet spritesheet.
 * All calls must come from the LVGL task (the app drives it from an
 * lv_timer; cross-thread state arrives via PetBridge polling).
 */
class PetRenderer {
public:
    void init(lv_obj_t *parent);
    /** Switch to the row for this state; DISCONNECTED shows a gray placeholder. */
    void playState(AgentState state);
    /** Advance the animation. Call every ~150 ms from an LVGL timer. */
    void tick(uint32_t now_ms);

private:
    lv_obj_t *_image = nullptr;
    lv_obj_t *_disconnected_box = nullptr;
    lv_obj_t *_label = nullptr;
    AgentState _state = PET_STATE_DISCONNECTED;
    uint8_t _frame = 0;
    bool _done = false; // one-shot finished, holding last frame
    uint32_t _last_advance = 0;
};

} // namespace pet_render
