#pragma once

#include "pet_bridge.h"
#include "lvgl.h"

namespace pet_render {

/** Uppercase display name for a state (shared with the app's list page). */
const char *stateName(AgentState state);

/**
 * Plays the sprite rows generated from the Codex-pet spritesheet.
 * All calls must come from the LVGL task (the app drives it from an
 * lv_timer; cross-thread state arrives via PetBridge polling).
 */
class PetRenderer {
public:
    void init(lv_obj_t *parent);
    /** Delete all LVGL objects created by init(). Call from close(). */
    void deinit(void);
    /** Switch to the row for this state. DISCONNECTED dims the pet with a
     *  blinking reconnect indicator; ERROR blinks a red border + badge. */
    void playState(AgentState state);
    /** Play the waving row once, then fall back to the current state. */
    void playIntro(void);
    /** Hide/show the whole pet (the app's list page replaces it). */
    void setVisible(bool visible);
    /** Advance animation + blink indicators. Call every ~150 ms. */
    void tick(uint32_t now_ms);

private:
    void applyVisibility(void);

    lv_obj_t *_image = nullptr;
    lv_obj_t *_overlay = nullptr;          // translucent dim over the pet (DISCONNECTED)
    lv_obj_t *_reconnect_label = nullptr;  // blinking "Reconnecting..." (DISCONNECTED)
    lv_obj_t *_error_border = nullptr;     // blinking red border (ERROR)
    lv_obj_t *_error_badge = nullptr;      // "!" badge (ERROR)
    lv_obj_t *_label = nullptr;            // bottom state label
    AgentState _state = PET_STATE_DISCONNECTED;
    bool _visible = true;                  // page-level visibility (list page hides the pet)
    uint8_t _frame = 0;
    bool _done = false; // one-shot finished, holding last frame
    bool _intro_active = false;
    uint8_t _intro_frame = 0;
    uint32_t _last_advance = 0;
    uint32_t _last_blink = 0;
    bool _blink_on = false;
};

} // namespace pet_render
