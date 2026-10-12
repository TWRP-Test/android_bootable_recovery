#include "shell/splash.h"

#include <algorithm>
#include <cstring>

#include "core/ui_helpers.h"
#include "gui2_splash_assets.h"

namespace gui2_shell {

namespace {

constexpr uint32_t kStatusText = 0x7C7C82;
constexpr uint32_t kDimText = 0x4A4A4F;
constexpr uint32_t kTrack = 0x1E1E21;
constexpr uint32_t kFill = 0x8E8E93;
constexpr uint32_t kBarMs = 400;
constexpr uint32_t kFadeMs = 300;

// Every part draws at this fraction of the design render, scaled by 1000.
int logo_scale = 1000;
bool intro_finished = false;
lv_obj_t* arrow = nullptr;

lv_obj_t* create_label(lv_obj_t* parent, const lv_font_t* font, uint32_t color, const char* text) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text == nullptr ? "" : text);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
  return label;
}

// Values are in thousandths of the part's own size.
void set_part_scale(void* target, int32_t value) {
  lv_image_set_scale(static_cast<lv_obj_t*>(target),
                     static_cast<uint32_t>(LV_SCALE_NONE * logo_scale / 1000 * value / 1000));
}

void set_opacity(void* target, int32_t value) {
  lv_obj_set_style_opa(static_cast<lv_obj_t*>(target), static_cast<lv_opa_t>(value), LV_PART_MAIN);
}

void set_rotation(void* target, int32_t value) {
  lv_image_set_rotation(static_cast<lv_obj_t*>(target), value);
}

void set_fill_width(void* target, int32_t value) {
  lv_obj_set_width(static_cast<lv_obj_t*>(target), value);
}

void animate(lv_obj_t* target, lv_anim_exec_xcb_t exec, int32_t from, int32_t to, uint32_t duration,
             uint32_t delay, lv_anim_path_cb_t path, lv_anim_completed_cb_t completed = nullptr) {
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, target);
  lv_anim_set_exec_cb(&anim, exec);
  lv_anim_set_values(&anim, from, to);
  lv_anim_set_duration(&anim, duration);
  lv_anim_set_delay(&anim, delay);
  lv_anim_set_path_cb(&anim, path);
  lv_anim_set_early_apply(&anim, true);
  if (completed != nullptr) lv_anim_set_completed_cb(&anim, completed);
  lv_anim_start(&anim);
}

void spin_arrow(lv_anim_t*) {
  intro_finished = true;
  if (arrow == nullptr) return;
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, arrow);
  lv_anim_set_exec_cb(&anim, set_rotation);
  lv_anim_set_values(&anim, 0, 3600);
  lv_anim_set_duration(&anim, 2400);
  lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&anim);
}

// Letters pop in one after another, then the arcs, the arrow and the credit.
void create_logo(lv_obj_t* root, const gui2_core::ui_metrics& metrics) {
  const int width = metrics.width * 78 / 100;
  logo_scale = width * 1000 / kGui2SplashWidth;
  const int height = kGui2SplashHeight * logo_scale / 1000;

  lv_obj_t* logo = lv_obj_create(root);
  lv_obj_set_size(logo, width, height);
  gui2_core::set_surface_style(logo, lv_color_hex(0x000000), LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(logo, 0, LV_PART_MAIN);
  lv_obj_set_overflow_visible(logo, true);
  gui2_core::disable_scrolling(logo);
  lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, metrics.height * 42 / 100 - height / 2);

  intro_finished = false;
  arrow = nullptr;
  int letter = 0;
  for (const gui2_splash_part& part : kGui2SplashParts) {
    const int w = static_cast<int>(part.image.header.w);
    const int h = static_cast<int>(part.image.header.h);
    lv_obj_t* image = lv_image_create(logo);
    lv_image_set_src(image, &part.image);
    lv_obj_set_style_image_recolor(image, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, LV_PART_MAIN);
    lv_image_set_pivot(image, w / 2, h / 2);
    lv_obj_set_pos(image, (part.x + w / 2) * logo_scale / 1000 - w / 2,
                   (part.y + h / 2) * logo_scale / 1000 - h / 2);
    set_part_scale(image, 1000);
    lv_obj_set_clickable(image, false);

    const char* name = part.name;
    if (std::strlen(name) == 1) {
      const uint32_t delay = 90 * letter++;
      animate(image, set_part_scale, 0, 1000, 420, delay, lv_anim_path_overshoot);
      animate(image, set_opacity, LV_OPA_TRANSP, LV_OPA_COVER, 420, delay, lv_anim_path_ease_out);
    } else if (std::strcmp(name, "arrow") == 0) {
      arrow = image;
      animate(image, set_part_scale, 400, 1000, 400, 480, lv_anim_path_ease_out);
      animate(image, set_rotation, -1200, 0, 400, 480, lv_anim_path_ease_out);
      animate(image, set_opacity, LV_OPA_TRANSP, LV_OPA_COVER, 400, 480, lv_anim_path_ease_out);
    } else if (std::strcmp(name, "based") == 0) {
      animate(image, set_opacity, LV_OPA_TRANSP, LV_OPA_COVER, 360, 650, lv_anim_path_ease_out,
              spin_arrow);
    } else {
      const uint32_t delay = std::strcmp(name, "top") == 0 ? 450 : 500;
      animate(image, set_opacity, LV_OPA_TRANSP, LV_OPA_COVER, 360, delay, lv_anim_path_ease_out);
    }
  }
}

}  // namespace

splash_view create_splash(const splash_options& options) {
  splash_view view;
  if (options.layer == nullptr || options.metrics == nullptr || options.text_font == nullptr ||
      options.small_font == nullptr)
    return view;

  const auto& metrics = *options.metrics;
  view.root = lv_obj_create(options.layer);
  lv_obj_set_pos(view.root, 0, 0);
  lv_obj_set_size(view.root, metrics.width, metrics.height);
  gui2_core::set_surface_style(view.root, lv_color_hex(0x000000), LV_OPA_COVER);
  lv_obj_set_style_radius(view.root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(view.root, 0, LV_PART_MAIN);
  gui2_core::disable_scrolling(view.root);
  lv_obj_set_clickable(view.root, true);

  create_logo(view.root, metrics);

  view.details = lv_obj_create(view.root);
  lv_obj_set_size(view.details, metrics.width, LV_SIZE_CONTENT);
  gui2_core::set_surface_style(view.details, lv_color_hex(0x000000), LV_OPA_TRANSP);
  lv_obj_set_style_pad_all(view.details, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(view.details, gui2_core::ui_px(28), LV_PART_MAIN);
  lv_obj_set_layout(view.details, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(view.details, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(view.details, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  gui2_core::disable_scrolling(view.details);
  lv_obj_align(view.details, LV_ALIGN_TOP_MID, 0, metrics.height * 63 / 100);
  lv_obj_set_style_opa(view.details, LV_OPA_TRANSP, LV_PART_MAIN);

  view.status = create_label(view.details, options.text_font, kStatusText, "");

  view.track_width = std::max(1, metrics.width * 36 / 100);
  const int bar_height = std::max(4, gui2_core::ui_px(10));
  lv_obj_t* track = lv_obj_create(view.details);
  lv_obj_set_size(track, view.track_width, bar_height);
  gui2_core::set_surface_style(track, lv_color_hex(kTrack), LV_OPA_COVER);
  lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_pad_all(track, 0, LV_PART_MAIN);
  lv_obj_set_style_clip_corner(track, true, LV_PART_MAIN);
  gui2_core::disable_scrolling(track);

  view.bar_fill = lv_obj_create(track);
  lv_obj_set_pos(view.bar_fill, 0, 0);
  lv_obj_set_size(view.bar_fill, 0, bar_height);
  gui2_core::set_surface_style(view.bar_fill, lv_color_hex(kFill), LV_OPA_COVER);
  lv_obj_set_style_radius(view.bar_fill, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  gui2_core::disable_scrolling(view.bar_fill);

  const int bottom = metrics.height * 6 / 100;
  lv_obj_t* device = create_label(view.root, options.small_font, kDimText, options.device);
  lv_obj_align(device, LV_ALIGN_BOTTOM_MID, 0, -bottom);
  lv_obj_t* version = create_label(view.root, options.text_font, kStatusText, options.version);
  lv_obj_align(version, LV_ALIGN_BOTTOM_MID, 0,
               -(bottom + lv_font_get_line_height(options.small_font) + gui2_core::ui_px(6)));
  return view;
}

bool splash_intro_finished() {
  return intro_finished;
}

void show_splash_details(splash_view* view) {
  if (view == nullptr || view->root == nullptr || view->details_shown) return;
  view->details_shown = true;
  lv_obj_fade_in(view->details, kFadeMs, 0);
}

void update_splash(splash_view* view, const char* text, float fraction) {
  if (view == nullptr || view->root == nullptr) return;

  const char* shown = lv_label_get_text(view->status);
  if (text != nullptr && (shown == nullptr || std::strcmp(shown, text) != 0))
    lv_label_set_text(view->status, text);

  const int target =
      static_cast<int>(static_cast<float>(view->track_width) * std::clamp(fraction, 0.0f, 1.0f));
  if (target == view->shown_width) return;
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, view->bar_fill);
  lv_anim_set_exec_cb(&anim, set_fill_width);
  lv_anim_set_values(&anim, lv_obj_get_width(view->bar_fill), target);
  lv_anim_set_duration(&anim, kBarMs);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
  lv_anim_start(&anim);
  view->shown_width = target;
}

void dismiss_splash(splash_view* view) {
  if (view == nullptr || view->root == nullptr) return;
  lv_obj_fade_out(view->root, kFadeMs, 0);
  lv_obj_delete_delayed(view->root, kFadeMs + 20);
  arrow = nullptr;
  *view = {};
}

}  // namespace gui2_shell
