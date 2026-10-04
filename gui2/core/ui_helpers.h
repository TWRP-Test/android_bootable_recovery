#ifndef GUI2_CORE_UI_HELPERS_H
#define GUI2_CORE_UI_HELPERS_H

#include "lvgl.h"

namespace gui2_core {

// Common LVGL surface setup used by shell, pages, and reusable components.
void set_surface_style(lv_obj_t* object, lv_color_t color,
                       lv_opa_t opa = LV_OPA_COVER);

// Containers are non-scrollable by default. The page content viewport is the
// explicit exception and enables scrolling at its call site.
void disable_scrolling(lv_obj_t* object);

// A thin bar on the right edge of anything that scrolls, so a long page shows
// how much of it is left.
void style_scrollbar(lv_obj_t* object, lv_color_t color);

// Lets a flex column of cards wrap into grid_columns() columns on a wide screen.
void set_card_grid(lv_obj_t* body);
// Accent fill when selected, `idle` otherwise; the card's own labels follow in on_accent.
void set_selected_fill(lv_obj_t* card, bool selected, lv_color_t idle);
// The card drop shadow; the light palette keeps cards flat.
void set_card_shadow(lv_obj_t* card);
// One grid column inside a body set_card_grid() split, else the full content width.
int card_width_in(lv_obj_t* parent);
// Cards sharing a grid row under `root` take the tallest one's height.
void equalize_card_grids(lv_obj_t* root);

}  // namespace gui2_core

#endif
