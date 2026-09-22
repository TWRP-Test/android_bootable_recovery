#ifndef GUI2_SHELL_NAVIGATION_FADE_H
#define GUI2_SHELL_NAVIGATION_FADE_H

#include "core/ui_metrics.h"
#include "lvgl.h"

namespace gui2_shell {

// reserved is what the page keeps clear at the bottom for a floating control;
// the gradient has to cover that too, or the content collides with it.
lv_obj_t* create_navigation_fade(lv_obj_t* page_layer, const gui2_core::ui_metrics& metrics,
                                 int reserved = 0);

// Keeps the gradient tied to the scroll position: it is there to hide content
// sliding under the navigation, so a page that does not scroll, and a page
// already scrolled to its end, must not show one.
void bind_navigation_fade(lv_obj_t* fade, lv_obj_t* content);
// animate is off for a page's first state: a rebuilt page would otherwise
// drop the gradient the old one was showing and fade it back in.
void update_navigation_fade(lv_obj_t* fade, lv_obj_t* content, bool animate = true);

}  // namespace gui2_shell

#endif
