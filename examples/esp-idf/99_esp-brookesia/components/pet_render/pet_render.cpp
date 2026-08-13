#include "pet_render.h"

#include "pet_frames.h"
#include "esp_lib_utils.h"

namespace pet_render {

namespace {

constexpr uint32_t FRAME_MS = 150;

const char *STATE_LABELS[PET_STATE_COUNT] = {
    "Idle", "Thinking", "Working", "Attention", "Error", "Disconnected",
};

} // namespace

void PetRenderer::init(lv_obj_t *parent)
{
    _image = lv_image_create(parent);
    lv_obj_align(_image, LV_ALIGN_CENTER, 0, -20);

    // Gray placeholder for DISCONNECTED (no sprite row for it)
    _disconnected_box = lv_obj_create(parent);
    lv_obj_set_size(_disconnected_box, PET_FRAME_W, PET_FRAME_H);
    lv_obj_align(_disconnected_box, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_radius(_disconnected_box, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_disconnected_box, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_border_width(_disconnected_box, 0, LV_PART_MAIN);
    lv_obj_t *box_label = lv_label_create(_disconnected_box);
    lv_label_set_text(box_label, "Offline");
    lv_obj_center(box_label);
    lv_obj_set_style_text_color(box_label, lv_color_hex(0xA0A0A0), LV_PART_MAIN);

    _label = lv_label_create(parent);
    lv_obj_align(_label, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_text_color(_label, lv_color_hex(0xC0C0FF), LV_PART_MAIN);
    lv_obj_set_style_text_font(_label, &lv_font_montserrat_20, LV_PART_MAIN);

    /* Initial visual: disconnected (playState would early-return — same state). */
    _state = PET_STATE_DISCONNECTED;
    lv_obj_add_flag(_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_disconnected_box, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(_label, STATE_LABELS[PET_STATE_DISCONNECTED]);
}

void PetRenderer::playState(AgentState state)
{
    if (state == _state) {
        return;
    }
    _state = state;
    _frame = 0;
    _done = false;
    _last_advance = 0;

    if (state == PET_STATE_DISCONNECTED) {
        lv_obj_add_flag(_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_disconnected_box, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_disconnected_box, LV_OBJ_FLAG_HIDDEN);
        const pet_state_anim_t &anim = pet_anims[state];
        if (anim.count > 0) {
            lv_image_set_src(_image, anim.frames[0]);
        }
    }
    lv_label_set_text(_label, STATE_LABELS[state]);
}

void PetRenderer::tick(uint32_t now_ms)
{
    if (_state == PET_STATE_DISCONNECTED || _done) {
        return;
    }
    const pet_state_anim_t &anim = pet_anims[_state];
    if (anim.count <= 1) {
        return;
    }
    if (now_ms - _last_advance < FRAME_MS) {
        return;
    }
    _last_advance = now_ms;

    _frame++;
    if (_frame >= anim.count) {
        if (anim.loop) {
            _frame = 0;
        } else {
            _done = true; // one-shot: hold the last frame
            _frame = anim.count - 1;
        }
    }
    lv_image_set_src(_image, anim.frames[_frame]);
}

} // namespace pet_render
