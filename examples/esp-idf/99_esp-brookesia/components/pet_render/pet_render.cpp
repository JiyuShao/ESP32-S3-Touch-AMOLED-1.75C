#include "pet_render.h"

#include "pet_frames.h"
#include "pet_theme.h"
#include "esp_lib_utils.h"

namespace pet_render {

namespace {

constexpr uint32_t FRAME_MS = 150;
constexpr uint32_t BLINK_MS = 400;

const char *STATE_LABELS[PET_STATE_COUNT] = {
    "IDLE", "THINKING", "WORKING", "ATTENTION", "ERROR", "DISCONNECTED",
};

} // namespace

const char *stateName(AgentState state)
{
    return STATE_LABELS[state];
}

void PetRenderer::init(lv_obj_t *parent)
{
    _image = lv_image_create(parent);
    lv_image_set_src(_image, pet_anims[PET_STATE_IDLE].frames[0]); // dimmed pet while disconnected
    /* Align AFTER the source is set: with a NULL src the image widget is
     * 0x0 and lv_obj_align would anchor it to (0,0) — off-center and
     * clipped by the shell's bars. */
    lv_obj_align(_image, LV_ALIGN_CENTER, 0, -20);
    const lv_image_dsc_t *dsc = (const lv_image_dsc_t *)lv_image_get_src(_image);
    ESP_UTILS_LOGD("img dsc %dx%d cf=%x, obj %dx%d@(%d,%d)",
                   dsc ? dsc->header.w : -1, dsc ? dsc->header.h : -1,
                   dsc ? dsc->header.cf : 0,
                   lv_obj_get_width(_image), lv_obj_get_height(_image),
                   lv_obj_get_x(_image), lv_obj_get_y(_image));

    // translucent dim over the pet while disconnected (created after image → on top)
    _overlay = lv_obj_create(parent);
    lv_obj_set_size(_overlay, PET_FRAME_W, PET_FRAME_H);
    lv_obj_align(_overlay, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_radius(_overlay, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_overlay, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_overlay, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(_overlay, 0, LV_PART_MAIN);
    lv_obj_add_flag(_overlay, LV_OBJ_FLAG_EVENT_BUBBLE); // let swipes reach the app's screen handler

    _reconnect_label = lv_label_create(parent);
    lv_label_set_text(_reconnect_label, "Reconnecting...");
    lv_obj_align(_reconnect_label, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_text_color(_reconnect_label, pet_theme::current()->fg, LV_PART_MAIN);
    lv_obj_set_style_text_font(_reconnect_label, &lv_font_montserrat_16, LV_PART_MAIN);

    // red blinking border + badge for ERROR
    _error_border = lv_obj_create(parent);
    lv_obj_set_size(_error_border, PET_FRAME_W, PET_FRAME_H);
    lv_obj_align(_error_border, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_radius(_error_border, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_error_border, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_error_border, 4, LV_PART_MAIN);
    lv_obj_set_style_border_color(_error_border, pet_theme::current()->error_border, LV_PART_MAIN);
    lv_obj_set_style_border_opa(_error_border, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_flag(_error_border, LV_OBJ_FLAG_EVENT_BUBBLE); // let swipes reach the app's screen handler

    _error_badge = lv_label_create(parent);
    lv_label_set_text(_error_badge, "!");
    lv_obj_align(_error_badge, LV_ALIGN_CENTER, PET_FRAME_W / 2 - 14, -20 - PET_FRAME_H / 2 + 12);
    lv_obj_set_style_text_color(_error_badge, pet_theme::current()->error_badge_text, LV_PART_MAIN);
    lv_obj_set_style_text_font(_error_badge, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_error_badge, pet_theme::current()->error_border, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_error_badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(_error_badge, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_error_badge, 4, LV_PART_MAIN);

    _label = lv_label_create(parent);
    lv_obj_align(_label, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_text_font(_label, &lv_font_montserrat_20, LV_PART_MAIN);

    /* Initial visual: disconnected (playState would early-return — same state). */
    _state = PET_STATE_DISCONNECTED;
    lv_label_set_text(_label, STATE_LABELS[PET_STATE_DISCONNECTED]);
    lv_obj_set_style_text_color(_label, pet_theme::current()->state[PET_STATE_DISCONNECTED], LV_PART_MAIN);
    applyVisibility();
}

void PetRenderer::deinit(void)
{
    // The shell keeps the app screen across close/run, so the app owns
    // deleting its own objects (brookesia's resource recorder is unused).
    if (_image != nullptr) {
        lv_obj_del(_image);
        _image = nullptr;
    }
    if (_overlay != nullptr) {
        lv_obj_del(_overlay);
        _overlay = nullptr;
    }
    if (_reconnect_label != nullptr) {
        lv_obj_del(_reconnect_label);
        _reconnect_label = nullptr;
    }
    if (_error_border != nullptr) {
        lv_obj_del(_error_border);
        _error_border = nullptr;
    }
    if (_error_badge != nullptr) {
        lv_obj_del(_error_badge);
        _error_badge = nullptr;
    }
    if (_label != nullptr) {
        lv_obj_del(_label);
        _label = nullptr;
    }
    _state = PET_STATE_DISCONNECTED;
    _intro_active = false;
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
    if (!_intro_active) {
        const pet_state_anim_t &anim = pet_anims[state];
        if (anim.count > 0) {
            lv_image_set_src(_image, anim.frames[0]);
        }
    }
    lv_label_set_text(_label, STATE_LABELS[state]);
    lv_obj_set_style_text_color(_label, pet_theme::current()->state[state], LV_PART_MAIN);
    applyVisibility();
}

void PetRenderer::playIntro(void)
{
    if (pet_intro_anim.count == 0) {
        return;
    }
    _intro_active = true;
    _intro_frame = 0;
    _last_advance = 0;
    lv_image_set_src(_image, pet_intro_anim.frames[0]);
}

void PetRenderer::setVisible(bool visible)
{
    _visible = visible;
    applyVisibility();
}

void PetRenderer::applyVisibility(void)
{
    bool disconnected = (_state == PET_STATE_DISCONNECTED);
    bool error = (_state == PET_STATE_ERROR);

    if (_visible) {
        lv_obj_clear_flag(_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (_visible && disconnected) {
        lv_obj_clear_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_reconnect_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_reconnect_label, LV_OBJ_FLAG_HIDDEN);
    }
    if (_visible && error) {
        lv_obj_clear_flag(_error_border, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_error_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_error_border, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(_error_badge, LV_OBJ_FLAG_HIDDEN);
    }
}

void PetRenderer::tick(uint32_t now_ms)
{
    if (!_layout_logged) {
        _layout_logged = true;
        ESP_UTILS_LOGD("after-layout img %dx%d@(%d,%d)",
                       lv_obj_get_width(_image), lv_obj_get_height(_image),
                       lv_obj_get_x(_image), lv_obj_get_y(_image));
    }
    // blink the state indicators (never while the page is hidden)
    if (_visible && now_ms - _last_blink >= BLINK_MS) {
        _last_blink = now_ms;
        _blink_on = !_blink_on;
        if (_state == PET_STATE_ERROR) {
            lv_obj_set_style_border_opa(_error_border, _blink_on ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
            if (_blink_on) {
                lv_obj_clear_flag(_error_badge, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(_error_badge, LV_OBJ_FLAG_HIDDEN);
            }
        } else if (_state == PET_STATE_DISCONNECTED) {
            if (_blink_on) {
                lv_obj_clear_flag(_reconnect_label, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(_reconnect_label, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // waving intro plays first, one-shot, then falls back to the state row
    if (_intro_active) {
        uint32_t intro_ms = pet_intro_anim.frame_ms ? pet_intro_anim.frame_ms : FRAME_MS;
        if (now_ms - _last_advance >= intro_ms) {
            _last_advance = now_ms;
            _intro_frame++;
            if (_intro_frame >= pet_intro_anim.count) {
                _intro_active = false;
                const pet_state_anim_t &anim = pet_anims[_state];
                if (anim.count > 0) {
                    lv_image_set_src(_image, anim.frames[0]);
                }
                return;
            }
            lv_image_set_src(_image, pet_intro_anim.frames[_intro_frame]);
            ESP_UTILS_LOGD("anim: intro frame %d/%d", _intro_frame, pet_intro_anim.count);
        }
        return;
    }

    if (_state == PET_STATE_DISCONNECTED || _done) {
        return;
    }
    const pet_state_anim_t &anim = pet_anims[_state];
    if (anim.count <= 1) {
        return;
    }
    uint32_t anim_ms = anim.frame_ms ? anim.frame_ms : FRAME_MS;
    if (now_ms - _last_advance < anim_ms) {
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
    ESP_UTILS_LOGD("anim: frame %d/%d (state=%s, %lums)", _frame, anim.count,
                   STATE_LABELS[_state], (unsigned long)anim_ms);
}

} // namespace pet_render
