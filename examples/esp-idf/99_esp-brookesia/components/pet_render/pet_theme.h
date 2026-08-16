#pragma once

/*
 * Pet app theme registry (header-only). All colors the app draws come from
 * here — switching themes means adding a palette and flipping current().
 * The shell's own dark stylesheet is untouched; these are the app's accents.
 *
 * Palette 1: Monokai.
 *   bg #272822 (shell's default dark)  fg #F8F8F2   comment #75715E
 *   red #F92672  orange #FD971F  yellow #E6DB74
 *   green #A6E22E  cyan #66D9EF  purple #AE81FF
 */
#include "lvgl.h"
#include "pet_bridge.h" // AgentState / PET_STATE_COUNT

namespace pet_theme {

struct Theme {
    lv_color_t bg;                      // app page background
    lv_color_t fg;                      // headings / primary text
    lv_color_t comment;                 // dim text (empty state, DISCONNECTED)
    lv_color_t dot_active;              // page dots
    lv_color_t dot_inactive;
    lv_color_t state[PET_STATE_COUNT];  // accent per display state
    lv_color_t error_border;
    lv_color_t error_badge_text;
};

inline const Theme kMonokai{
    /* bg */        lv_color_hex(0x272822),
    /* fg */        lv_color_hex(0xF8F8F2),
    /* comment */   lv_color_hex(0x75715E),
    /* dot_active */    lv_color_hex(0xF8F8F2),
    /* dot_inactive */  lv_color_hex(0x49483E),
    /* state */ {
        lv_color_hex(0xA6E22E),  // IDLE green
        lv_color_hex(0x66D9EF),  // THINKING cyan
        lv_color_hex(0xFD971F),  // WORKING orange
        lv_color_hex(0xE6DB74),  // ATTENTION yellow
        lv_color_hex(0xF92672),  // ERROR pink-red
        lv_color_hex(0x75715E),  // DISCONNECTED comment
    },
    /* error_border */      lv_color_hex(0xF92672),
    /* error_badge_text */  lv_color_hex(0xF8F8F2),
};

/* Active theme — add a palette above and flip this to switch. */
inline const Theme *current(void)
{
    return &kMonokai;
}

} // namespace pet_theme
