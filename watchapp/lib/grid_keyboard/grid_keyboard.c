#include "grid_keyboard.h"

#include <string.h>

#ifdef PBL_PLATFORM_EMERY
#define GK_TOUCH 1
#endif

// ---------------------------------------------------------------------------
// Key tables
// ---------------------------------------------------------------------------
//
// Nine cells, three characters each. Every choice names the neighbouring cell
// it fans out to when the key is held, which is what the swipe is matched
// against - so the gesture is "flick toward that square" rather than "flick in
// some memorised compass direction".
//
// Choice 1 is special: it is also the character a plain tap produces, and its
// target may be the cell itself (meaning tap-only, no swipe reaches it).

typedef struct {
  char character;
  int8_t target;
} GkChoice;

// abc / def / ghi / jkl / m-n / opq / rst / uvw / xyz.
// Taps give b, e, h, k, space, p, s, v, y.
static const GkChoice GK_LETTERS[9][3] = {
  {{'a', 6}, {'b', 8}, {'c', 2}},
  {{'d', 6}, {'e', 7}, {'f', 8}},
  {{'g', 8}, {'h', 6}, {'i', 0}},
  {{'j', 2}, {'k', 5}, {'l', 8}},
  {{'m', 3}, {' ', 4}, {'n', 5}},
  {{'o', 0}, {'p', 3}, {'q', 6}},
  {{'r', 0}, {'s', 2}, {'t', 8}},
  {{'u', 0}, {'v', 1}, {'w', 2}},
  {{'x', 2}, {'y', 0}, {'z', 6}},
};

// Digits sit on the taps so the number pad reads exactly like a phone keypad;
// punctuation hangs off the same swipe directions as the letters. Zero keeps
// its home on the "8" cell, as it has to - there is no tenth square.
static const GkChoice GK_SYMBOLS[9][3] = {
  {{'!', 6}, {'1', 0}, {'?', 2}},
  {{'(', 6}, {'2', 1}, {')', 8}},
  {{'#', 8}, {'3', 2}, {'@', 0}},
  {{'&', 2}, {'4', 3}, {'*', 8}},
  {{'-', 3}, {'5', 4}, {'+', 5}},
  {{'=', 0}, {'6', 5}, {'/', 6}},
  {{':', 0}, {'7', 6}, {';', 8}},
  {{'.', 0}, {'8', 7}, {'0', 2}},
  {{'\'', 2}, {'9', 8}, {',', 6}},
};

static const char *const GK_LETTER_LABELS[9] = {
  "abc", "def", "ghi", "jkl", "m  n", "opq", "rst", "uvw", "xyz",
};
static const char *const GK_SYMBOL_LABELS[9] = {
  "!1?", "(2)", "#3@", "&4*", "-5+", "=6/", ":7;", ".80", "'9,",
};

#define GK_SWIPE_MIN_DIST 14
#define GK_SWIPE_MIN_DIST_SQ (GK_SWIPE_MIN_DIST * GK_SWIPE_MIN_DIST)

// ---------------------------------------------------------------------------

struct GridKeyboard {
  Window *window;
  Layer *layer;

  char *buffer;
  size_t buffer_size;
  size_t length;

  const char *title;
  const char *placeholder;
  GColor accent;

  GridKeyboardResultHandler result_handler;
  GridKeyboardCancelHandler cancel_handler;
  void *context;

  uint8_t mode;
  bool allow_empty;
  bool vibes;

  bool auto_destroy;
  bool accepted;
  bool suppress_handlers;

  // Button driving: focus walks the nine cells, and opening a cell fans its
  // three characters onto UP / SELECT / DOWN.
  bool button_mode;
  int8_t focus;
  int8_t fan_open;

  // Touch driving.
  bool touch_active;
  int8_t touch_badge;
  int8_t touch_origin;
  int8_t touch_target;
  int16_t touch_x;
  int16_t touch_y;
  bool touch_subscribed;

  bool help_visible;
  AppTimer *help_timer;
  AppTimer *destroy_timer;
};

enum {
  GkBadgeNone = -1,
  GkBadgeMode = 0,
  GkBadgeHelp = 1,
  GkBadgeGo = 2,
};

// ---------------------------------------------------------------------------
// Layout, derived from the layer bounds so the keyboard fits whatever display
// it is dropped onto rather than the one it was written on.
// ---------------------------------------------------------------------------

typedef struct {
  GRect field;
  GRect text;
  GRect grid;
  GRect badge_mode;
  GRect badge_help;
  GRect badge_go;
  int16_t cell_w;
  int16_t cell_h;
  int16_t gap;
} GkLayout;

static void gk_layout(const GridKeyboard *keyboard, GRect bounds, GkLayout *out) {
  const int16_t pad = 2;
  const int16_t gap = 2;
  const bool has_title = keyboard->title && keyboard->title[0];
  int16_t field_h = bounds.size.h >= 200 ? 53 : 44;
  if (has_title) field_h += 14;

  out->gap = gap;
  out->field = GRect(pad, pad, bounds.size.w - pad * 2, field_h);

  const int16_t badge_h = 18;
  const int16_t badge_y = out->field.origin.y + field_h - badge_h - 3;
  const int16_t right = out->field.origin.x + out->field.size.w - 4;
  out->badge_go = GRect(right - 32, badge_y, 32, badge_h);
  out->badge_help = GRect(out->badge_go.origin.x - 20, badge_y, 20, badge_h);
  out->badge_mode = GRect(out->badge_help.origin.x - 32, badge_y, 32, badge_h);

  const int16_t text_y = out->field.origin.y + (has_title ? 20 : 5);
  out->text = GRect(out->field.origin.x + 6, text_y,
                    out->field.size.w - 12, badge_y - text_y - 1);

  const int16_t grid_top = out->field.origin.y + field_h + gap;
  out->grid = GRect(pad, grid_top, bounds.size.w - pad * 2,
                    bounds.size.h - grid_top - pad);
  out->cell_w = (out->grid.size.w - gap * 2) / 3;
  out->cell_h = (out->grid.size.h - gap * 2) / 3;
}

static GRect gk_cell_rect(const GkLayout *layout, int index) {
  int row = index / 3, col = index % 3;
  return GRect(layout->grid.origin.x + col * (layout->cell_w + layout->gap),
               layout->grid.origin.y + row * (layout->cell_h + layout->gap),
               layout->cell_w, layout->cell_h);
}

#ifdef GK_TOUCH
static bool gk_point_in(GRect rect, int16_t x, int16_t y) {
  return x >= rect.origin.x && x < rect.origin.x + rect.size.w &&
         y >= rect.origin.y && y < rect.origin.y + rect.size.h;
}

// Hit-tests against the whole grid rather than the individual cells, so the
// gaps belong to their nearest key instead of swallowing touches.
static int gk_cell_at(const GkLayout *layout, int16_t x, int16_t y) {
  if (!gk_point_in(layout->grid, x, y)) return -1;
  int col = (x - layout->grid.origin.x) * 3 / layout->grid.size.w;
  int row = (y - layout->grid.origin.y) * 3 / layout->grid.size.h;
  if (col < 0) col = 0;
  if (col > 2) col = 2;
  if (row < 0) row = 0;
  if (row > 2) row = 2;
  return row * 3 + col;
}
#endif  // GK_TOUCH

// ---------------------------------------------------------------------------
// Characters
// ---------------------------------------------------------------------------

static const GkChoice *gk_table(const GridKeyboard *keyboard) {
  return keyboard->mode == GridKeyboardModeSymbols ? &GK_SYMBOLS[0][0] : &GK_LETTERS[0][0];
}

static char gk_character(const GridKeyboard *keyboard, int cell, int choice) {
  if (cell < 0 || cell > 8 || choice < 0 || choice > 2) return '\0';
  char character = gk_table(keyboard)[cell * 3 + choice].character;
  if (keyboard->mode == GridKeyboardModeUpper && character >= 'a' && character <= 'z') {
    character -= 'a' - 'A';
  }
  return character;
}

static int8_t gk_target(const GridKeyboard *keyboard, int cell, int choice) {
  return gk_table(keyboard)[cell * 3 + choice].target;
}

static void gk_label(const GridKeyboard *keyboard, int cell, char *out, size_t out_size) {
  const char *label = keyboard->mode == GridKeyboardModeSymbols ? GK_SYMBOL_LABELS[cell]
                                                                : GK_LETTER_LABELS[cell];
  snprintf(out, out_size, "%s", label);
  if (keyboard->mode == GridKeyboardModeUpper) {
    for (int i = 0; out[i]; i++) {
      if (out[i] >= 'a' && out[i] <= 'z') out[i] -= 'a' - 'A';
    }
  }
}

static const char *gk_mode_label(const GridKeyboard *keyboard) {
  if (keyboard->mode == GridKeyboardModeUpper) return "ABC";
  if (keyboard->mode == GridKeyboardModeSymbols) return "123";
  return "abc";
}

#ifdef GK_TOUCH
// The few distinct grid-delta lengths a 3x3 layout can produce (1, sqrt2, 2,
// sqrt5, sqrt8 cells), scaled by 1000. Cheaper than pulling in sqrtf for what
// is really a three-way comparison.
static int32_t gk_dir_magnitude(int32_t mag_sq) {
  switch (mag_sq) {
    case 1: return 1000;
    case 2: return 1414;
    case 4: return 2000;
    case 5: return 2236;
    case 8: return 2828;
    default: return 1000;
  }
}

// Picks whichever of the origin cell's three choices best matches the swipe
// vector by angle, so the finger never has to actually land inside the
// destination square. Returns -1 when the swipe is too short to have a
// direction, which the caller reads as a tap.
static int8_t gk_best_choice(const GridKeyboard *keyboard, int origin, int16_t dx, int16_t dy) {
  if (origin < 0 || origin > 8) return -1;
  int32_t dist_sq = (int32_t) dx * dx + (int32_t) dy * dy;
  if (dist_sq < GK_SWIPE_MIN_DIST_SQ) return -1;
  int orow = origin / 3, ocol = origin % 3;
  int8_t best = -1;
  int32_t best_score = 0;
  for (int i = 0; i < 3; i++) {
    int target = gk_target(keyboard, origin, i);
    int32_t tx = (target % 3) - ocol;
    int32_t ty = (target / 3) - orow;
    int32_t mag_sq = tx * tx + ty * ty;
    if (mag_sq == 0) continue;  // tap-only choice
    int32_t dot = (int32_t) dx * tx + (int32_t) dy * ty;
    // Proportional to the cosine of the angle between swipe and choice. The
    // shared |swipe| factor is dropped: it is constant across the candidates
    // and cannot change the argmax.
    int32_t score = (dot * 1000) / gk_dir_magnitude(mag_sq);
    if (best < 0 || score > best_score) {
      best_score = score;
      best = (int8_t) i;
    }
  }
  return best;
}
#endif  // GK_TOUCH

// ---------------------------------------------------------------------------
// Buffer
// ---------------------------------------------------------------------------

// The layer only exists between window load and unload, so every redraw goes
// through here rather than assuming it is there.
static void gk_mark_dirty(GridKeyboard *keyboard) {
  if (keyboard->layer) layer_mark_dirty(keyboard->layer);
}

static void gk_insert(GridKeyboard *keyboard, char character) {
  if (character == '\0') return;
  if (keyboard->length + 1 >= keyboard->buffer_size) {
    if (keyboard->vibes) vibes_double_pulse();
    return;
  }
  keyboard->buffer[keyboard->length++] = character;
  keyboard->buffer[keyboard->length] = '\0';
  if (keyboard->vibes) vibes_short_pulse();
  gk_mark_dirty(keyboard);
}

static bool gk_backspace(GridKeyboard *keyboard) {
  if (keyboard->length == 0) return false;
  keyboard->buffer[--keyboard->length] = '\0';
  gk_mark_dirty(keyboard);
  return true;
}

static void gk_cycle_mode(GridKeyboard *keyboard) {
  keyboard->mode = (keyboard->mode + 1) % 3;
  keyboard->fan_open = -1;
  gk_mark_dirty(keyboard);
}

static void gk_accept(GridKeyboard *keyboard) {
  if (keyboard->length == 0 && !keyboard->allow_empty) {
    if (keyboard->vibes) vibes_double_pulse();
    return;
  }
  // Marked accepted and popped before the handler runs, so the handler is free
  // to push its own window without landing underneath this one.
  keyboard->accepted = true;
  window_stack_remove(keyboard->window, true);
  if (keyboard->result_handler) {
    keyboard->result_handler(keyboard->buffer, keyboard->context);
  }
}

// ---------------------------------------------------------------------------
// Help overlay
// ---------------------------------------------------------------------------

static void gk_help_timer_cb(void *context) {
  GridKeyboard *keyboard = context;
  keyboard->help_timer = NULL;
  keyboard->help_visible = false;
  gk_mark_dirty(keyboard);
}

static void gk_show_help(GridKeyboard *keyboard) {
  if (keyboard->help_timer) app_timer_cancel(keyboard->help_timer);
  keyboard->help_visible = true;
  keyboard->help_timer = app_timer_register(3500, gk_help_timer_cb, keyboard);
  gk_mark_dirty(keyboard);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void gk_draw_text(GContext *ctx, const char *text, GFont font, GColor color,
                         GRect rect, GTextAlignment alignment) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, rect, GTextOverflowModeTrailingEllipsis, alignment, NULL);
}

// Walks the string forward until what is left fits the field, so a long query
// scrolls with the cursor instead of hiding the characters just typed.
static const char *gk_visible_tail(const char *text, GFont font, GRect box) {
  const char *start = text;
  while (*start) {
    GSize size = graphics_text_layout_get_content_size(
        start, font, GRect(0, 0, box.size.w, 2000), GTextOverflowModeWordWrap,
        GTextAlignmentLeft);
    if (size.h <= box.size.h) break;
    start++;
  }
  return start;
}

static void gk_draw_badge(GContext *ctx, GRect rect, const char *label, GColor background,
                          GColor foreground) {
  graphics_context_set_fill_color(ctx, background);
  graphics_fill_rect(ctx, rect, 3, GCornersAll);
  gk_draw_text(ctx, label, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), foreground,
               GRect(rect.origin.x, rect.origin.y + 1, rect.size.w, rect.size.h),
               GTextAlignmentCenter);
}

static void gk_draw_help(GridKeyboard *keyboard, GContext *ctx, GRect bounds) {
  const int16_t w = bounds.size.w - 16;
  const int16_t h = 126;
  GRect card = GRect(8, (bounds.size.h - h) / 2, w, h);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, card, 8, GCornersAll);
  graphics_context_set_stroke_color(ctx, keyboard->accent);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_round_rect(ctx, card, 8);
  graphics_context_set_stroke_width(ctx, 1);

  GRect line = GRect(card.origin.x + 8, card.origin.y + 6, card.size.w - 16, 18);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  const char *lines[6];
  int count = 0;
#ifdef GK_TOUCH
  lines[count++] = "Tap a key: middle letter";
  lines[count++] = "Swipe toward a key: the";
  lines[count++] = "   letter shown on it";
#else
  lines[count++] = "UP / DOWN: move";
  lines[count++] = "SELECT: open key, then";
  lines[count++] = "   UP / SELECT / DOWN";
#endif
  lines[count++] = "Hold SELECT: accept";
  lines[count++] = "Hold UP: abc / ABC / 123";
  lines[count++] = "BACK: delete, then exit";
  for (int i = 0; i < count; i++) {
    gk_draw_text(ctx, lines[i], font, GColorWhite, line, GTextAlignmentLeft);
    line.origin.y += 19;
  }
}

static void gk_update_proc(Layer *layer, GContext *ctx) {
  GridKeyboard *keyboard = *(GridKeyboard **) layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);
  GkLayout layout;
  gk_layout(keyboard, bounds, &layout);

  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // Text field.
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, layout.field, 4, GCornersAll);
  if (keyboard->title && keyboard->title[0]) {
    gk_draw_text(ctx, keyboard->title, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
                 GColorDarkGray,
                 GRect(layout.field.origin.x + 6, layout.field.origin.y + 3,
                       layout.field.size.w - 12, 16),
                 GTextAlignmentLeft);
  }
  GFont text_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  if (keyboard->length > 0) {
    gk_draw_text(ctx, gk_visible_tail(keyboard->buffer, text_font, layout.text), text_font,
                 GColorBlack, layout.text, GTextAlignmentLeft);
  } else {
    gk_draw_text(ctx, keyboard->placeholder, text_font, GColorDarkGray, layout.text,
                 GTextAlignmentLeft);
  }

  gk_draw_badge(ctx, layout.badge_mode, gk_mode_label(keyboard),
                keyboard->touch_badge == GkBadgeMode ? keyboard->accent : GColorWhite,
                keyboard->touch_badge == GkBadgeMode ? GColorWhite : keyboard->accent);
  gk_draw_badge(ctx, layout.badge_help, "?",
                keyboard->touch_badge == GkBadgeHelp ? keyboard->accent : GColorWhite,
                keyboard->touch_badge == GkBadgeHelp ? GColorWhite : GColorDarkGray);
  bool go_enabled = keyboard->length > 0 || keyboard->allow_empty;
  gk_draw_badge(ctx, layout.badge_go, "GO",
                keyboard->touch_badge == GkBadgeGo ? keyboard->accent
                    : go_enabled ? GColorWhite : GColorLightGray,
                keyboard->touch_badge == GkBadgeGo ? GColorWhite
                    : go_enabled ? keyboard->accent : GColorDarkGray);

  // Grid.
  const bool fanning = keyboard->touch_active || keyboard->fan_open >= 0;
  const int fan_origin = keyboard->fan_open >= 0 ? keyboard->fan_open : keyboard->touch_origin;
  const int highlight = keyboard->fan_open >= 0 ? -1 : keyboard->touch_target;

  for (int i = 0; i < 9; i++) {
    GRect cell = gk_cell_rect(&layout, i);
    char display[8];
    const char *button_hint = NULL;
    bool dimmed = fanning;
    bool active = (i == highlight);

    if (fanning) {
      // While a key is open, its three characters are spread onto the cells
      // they are swiped toward - the destination is the label.
      display[0] = '\0';
      for (int choice = 0; choice < 3; choice++) {
        if (gk_target(keyboard, fan_origin, choice) != i) continue;
        display[0] = gk_character(keyboard, fan_origin, choice);
        if (display[0] == ' ') display[0] = '_';
        display[1] = '\0';
        dimmed = false;
        button_hint = keyboard->fan_open >= 0 ? (choice == 0 ? "UP" : choice == 2 ? "DOWN" : "SEL")
                                              : NULL;
        break;
      }
      // The tap-only choice has nowhere to fan to, so it stays on its own key.
      if (keyboard->fan_open >= 0 && i == fan_origin) {
        for (int choice = 0; choice < 3; choice++) {
          if (gk_target(keyboard, fan_origin, choice) != fan_origin) continue;
          display[0] = gk_character(keyboard, fan_origin, choice);
          if (display[0] == ' ') display[0] = '_';
          display[1] = '\0';
          dimmed = false;
          button_hint = choice == 0 ? "UP" : choice == 2 ? "DOWN" : "SEL";
          break;
        }
      }
    } else {
      gk_label(keyboard, i, display, sizeof(display));
    }

    graphics_context_set_fill_color(ctx, active ? keyboard->accent : GColorWhite);
    graphics_fill_rect(ctx, cell, 3, GCornersAll);
    if (!fanning && keyboard->button_mode && i == keyboard->focus) {
      graphics_context_set_stroke_color(ctx, keyboard->accent);
      graphics_context_set_stroke_width(ctx, 3);
      graphics_draw_round_rect(ctx, cell, 3);
      graphics_context_set_stroke_width(ctx, 1);
    }

    GColor glyph = active ? GColorWhite : dimmed ? GColorLightGray : GColorBlack;
    int16_t glyph_y = cell.origin.y + (cell.size.h - 23) / 2 - (button_hint ? 6 : 0);
    gk_draw_text(ctx, display, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD), glyph,
                 GRect(cell.origin.x + 2, glyph_y, cell.size.w - 4, 23), GTextAlignmentCenter);
    if (button_hint) {
      gk_draw_text(ctx, button_hint, fonts_get_system_font(FONT_KEY_GOTHIC_14),
                   active ? GColorWhite : GColorDarkGray,
                   GRect(cell.origin.x + 2, glyph_y + 19, cell.size.w - 4, 16),
                   GTextAlignmentCenter);
    }
  }

  if (keyboard->help_visible) gk_draw_help(keyboard, ctx, bounds);
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

static void gk_enter_button_mode(GridKeyboard *keyboard) {
  keyboard->button_mode = true;
}

static void gk_up_click(ClickRecognizerRef recognizer, void *context) {
  GridKeyboard *keyboard = context;
  gk_enter_button_mode(keyboard);
  if (keyboard->fan_open >= 0) {
    gk_insert(keyboard, gk_character(keyboard, keyboard->fan_open, 0));
    keyboard->fan_open = -1;
  } else {
    keyboard->focus = (keyboard->focus + 8) % 9;
  }
  gk_mark_dirty(keyboard);
}

static void gk_down_click(ClickRecognizerRef recognizer, void *context) {
  GridKeyboard *keyboard = context;
  gk_enter_button_mode(keyboard);
  if (keyboard->fan_open >= 0) {
    gk_insert(keyboard, gk_character(keyboard, keyboard->fan_open, 2));
    keyboard->fan_open = -1;
  } else {
    keyboard->focus = (keyboard->focus + 1) % 9;
  }
  gk_mark_dirty(keyboard);
}

static void gk_select_click(ClickRecognizerRef recognizer, void *context) {
  GridKeyboard *keyboard = context;
  gk_enter_button_mode(keyboard);
  if (keyboard->fan_open >= 0) {
    gk_insert(keyboard, gk_character(keyboard, keyboard->fan_open, 1));
    keyboard->fan_open = -1;
  } else {
    keyboard->fan_open = keyboard->focus;
  }
  gk_mark_dirty(keyboard);
}

static void gk_select_long_click(ClickRecognizerRef recognizer, void *context) {
  gk_accept((GridKeyboard *) context);
}

static void gk_up_long_click(ClickRecognizerRef recognizer, void *context) {
  gk_cycle_mode((GridKeyboard *) context);
}

// Hold DOWN to wipe the field rather than tapping BACK a dozen times.
static void gk_down_long_click(ClickRecognizerRef recognizer, void *context) {
  GridKeyboard *keyboard = context;
  if (keyboard->length == 0) return;
  keyboard->length = 0;
  keyboard->buffer[0] = '\0';
  keyboard->fan_open = -1;
  if (keyboard->vibes) vibes_short_pulse();
  gk_mark_dirty(keyboard);
}

// BACK closes an open key, then deletes, and only leaves once there is nothing
// left to delete - so a mistyped character never costs you the whole session.
static void gk_back_click(ClickRecognizerRef recognizer, void *context) {
  GridKeyboard *keyboard = context;
  if (keyboard->help_visible) {
    keyboard->help_visible = false;
    gk_mark_dirty(keyboard);
    return;
  }
  if (keyboard->fan_open >= 0) {
    keyboard->fan_open = -1;
    gk_mark_dirty(keyboard);
    return;
  }
  if (gk_backspace(keyboard)) return;
  window_stack_remove(keyboard->window, true);
}

// The "?" badge is a touch target, so BACK is the only button left to reach the
// cheat sheet from - which matters most on the watches that have no touchscreen
// and where the button model is all there is.
static void gk_back_long_click(ClickRecognizerRef recognizer, void *context) {
  gk_show_help((GridKeyboard *) context);
}

static void gk_click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, gk_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, gk_down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, gk_select_click);
  window_single_click_subscribe(BUTTON_ID_BACK, gk_back_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, gk_select_long_click, NULL);
  window_long_click_subscribe(BUTTON_ID_UP, 500, gk_up_long_click, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, 500, gk_down_long_click, NULL);
  window_long_click_subscribe(BUTTON_ID_BACK, 500, gk_back_long_click, NULL);
  window_set_click_context(BUTTON_ID_UP, context);
  window_set_click_context(BUTTON_ID_DOWN, context);
  window_set_click_context(BUTTON_ID_SELECT, context);
  window_set_click_context(BUTTON_ID_BACK, context);
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------

#ifdef GK_TOUCH
static void gk_touch_handler(const TouchEvent *event, void *context) {
  GridKeyboard *keyboard = context;
  if (!keyboard || !event || !keyboard->layer) return;
  GkLayout layout;
  gk_layout(keyboard, layer_get_bounds(keyboard->layer), &layout);

  switch (event->type) {
    case TouchEvent_Touchdown: {
      keyboard->fan_open = -1;
      keyboard->button_mode = false;
      if (gk_point_in(layout.badge_mode, event->x, event->y)) {
        keyboard->touch_badge = GkBadgeMode;
      } else if (gk_point_in(layout.badge_help, event->x, event->y)) {
        keyboard->touch_badge = GkBadgeHelp;
      } else if (gk_point_in(layout.badge_go, event->x, event->y)) {
        keyboard->touch_badge = GkBadgeGo;
      } else {
        int cell = gk_cell_at(&layout, event->x, event->y);
        if (cell < 0) return;
        keyboard->touch_active = true;
        keyboard->touch_origin = (int8_t) cell;
        keyboard->touch_target = (int8_t) cell;
        keyboard->touch_x = event->x;
        keyboard->touch_y = event->y;
      }
      if (keyboard->vibes) vibes_short_pulse();
      break;
    }
    case TouchEvent_PositionUpdate: {
      if (!keyboard->touch_active) break;
      int8_t choice = gk_best_choice(keyboard, keyboard->touch_origin,
                                     event->x - keyboard->touch_x,
                                     event->y - keyboard->touch_y);
      keyboard->touch_target = choice >= 0 ? gk_target(keyboard, keyboard->touch_origin, choice)
                                           : keyboard->touch_origin;
      break;
    }
    case TouchEvent_Liftoff: {
      int8_t badge = keyboard->touch_badge;
      bool was_active = keyboard->touch_active;
      int8_t origin = keyboard->touch_origin;
      int16_t dx = event->x - keyboard->touch_x;
      int16_t dy = event->y - keyboard->touch_y;

      keyboard->touch_badge = GkBadgeNone;
      keyboard->touch_active = false;
      keyboard->touch_origin = -1;
      keyboard->touch_target = -1;

      if (badge == GkBadgeMode && gk_point_in(layout.badge_mode, event->x, event->y)) {
        gk_cycle_mode(keyboard);
      } else if (badge == GkBadgeHelp && gk_point_in(layout.badge_help, event->x, event->y)) {
        gk_show_help(keyboard);
      } else if (badge == GkBadgeGo && gk_point_in(layout.badge_go, event->x, event->y)) {
        gk_accept(keyboard);
        return;  // the window is gone; do not touch the layer
      } else if (was_active) {
        // A short press is a tap and yields choice 1; anything longer is
        // resolved by angle against the three fan directions.
        int8_t choice = gk_best_choice(keyboard, origin, dx, dy);
        gk_insert(keyboard, gk_character(keyboard, origin, choice >= 0 ? choice : 1));
      }
      break;
    }
  }
  gk_mark_dirty(keyboard);
}
#endif

static void gk_sync_touch(GridKeyboard *keyboard, bool subscribe) {
#ifdef GK_TOUCH
  if (subscribe && !keyboard->touch_subscribed) {
    touch_service_subscribe(gk_touch_handler, keyboard);
    keyboard->touch_subscribed = true;
  } else if (!subscribe && keyboard->touch_subscribed) {
    touch_service_unsubscribe();
    keyboard->touch_subscribed = false;
    keyboard->touch_active = false;
    keyboard->touch_badge = GkBadgeNone;
  }
#else
  (void) keyboard;
  (void) subscribe;
#endif
}

bool grid_keyboard_touch_available(void) {
#ifdef GK_TOUCH
  return true;
#else
  return false;
#endif
}

// ---------------------------------------------------------------------------
// Window lifecycle
// ---------------------------------------------------------------------------

static void gk_window_load(Window *window) {
  GridKeyboard *keyboard = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  keyboard->layer = layer_create_with_data(bounds, sizeof(GridKeyboard *));
  *(GridKeyboard **) layer_get_data(keyboard->layer) = keyboard;
  layer_set_update_proc(keyboard->layer, gk_update_proc);
  layer_add_child(root, keyboard->layer);
}

static void gk_window_appear(Window *window) {
  gk_sync_touch(window_get_user_data(window), true);
}

static void gk_window_disappear(Window *window) {
  gk_sync_touch(window_get_user_data(window), false);
}

static void gk_destroy_timer_cb(void *context) {
  GridKeyboard *keyboard = context;
  keyboard->destroy_timer = NULL;
  grid_keyboard_destroy(keyboard);
}

static void gk_window_unload(Window *window) {
  GridKeyboard *keyboard = window_get_user_data(window);
  if (keyboard->layer) {
    layer_destroy(keyboard->layer);
    keyboard->layer = NULL;
  }
  if (!keyboard->accepted && !keyboard->suppress_handlers && keyboard->cancel_handler) {
    keyboard->cancel_handler(keyboard->context);
  }
  // Deferred: we are inside the window's own unload, so window_destroy() has to
  // wait until the SDK is done with it.
  if (keyboard->auto_destroy && !keyboard->destroy_timer) {
    keyboard->destroy_timer = app_timer_register(0, gk_destroy_timer_cb, keyboard);
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

GridKeyboard *grid_keyboard_create(char *buffer, size_t buffer_size) {
  if (!buffer || buffer_size < 2) return NULL;
  GridKeyboard *keyboard = malloc(sizeof(GridKeyboard));
  if (!keyboard) return NULL;
  memset(keyboard, 0, sizeof(GridKeyboard));

  keyboard->buffer = buffer;
  keyboard->buffer_size = buffer_size;
  buffer[buffer_size - 1] = '\0';
  keyboard->length = strlen(buffer);

  keyboard->placeholder = "Start typing...";
  keyboard->accent = PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorBlack);
  keyboard->vibes = true;
  keyboard->focus = 4;
  keyboard->fan_open = -1;
  keyboard->touch_origin = -1;
  keyboard->touch_target = -1;
  keyboard->touch_badge = GkBadgeNone;
  keyboard->button_mode = !grid_keyboard_touch_available();

  keyboard->window = window_create();
  if (!keyboard->window) {
    free(keyboard);
    return NULL;
  }
  window_set_user_data(keyboard->window, keyboard);
  window_set_click_config_provider_with_context(keyboard->window, gk_click_config, keyboard);
  window_set_window_handlers(keyboard->window, (WindowHandlers) {
    .load = gk_window_load,
    .appear = gk_window_appear,
    .disappear = gk_window_disappear,
    .unload = gk_window_unload,
  });
  return keyboard;
}

void grid_keyboard_destroy(GridKeyboard *keyboard) {
  if (!keyboard) return;
  if (keyboard->help_timer) app_timer_cancel(keyboard->help_timer);
  if (keyboard->destroy_timer) app_timer_cancel(keyboard->destroy_timer);
  gk_sync_touch(keyboard, false);
  if (keyboard->window) {
    keyboard->suppress_handlers = true;
    window_stack_remove(keyboard->window, false);
    window_destroy(keyboard->window);
  }
  free(keyboard);
}

GridKeyboard *grid_keyboard_show(char *buffer, size_t buffer_size,
                                 GridKeyboardResultHandler handler, void *context) {
  GridKeyboard *keyboard = grid_keyboard_create(buffer, buffer_size);
  if (!keyboard) return NULL;
  keyboard->auto_destroy = true;
  keyboard->result_handler = handler;
  keyboard->context = context;
  grid_keyboard_push(keyboard, true);
  return keyboard;
}

void grid_keyboard_set_handlers(GridKeyboard *keyboard,
                                GridKeyboardResultHandler result_handler,
                                GridKeyboardCancelHandler cancel_handler, void *context) {
  if (!keyboard) return;
  keyboard->result_handler = result_handler;
  keyboard->cancel_handler = cancel_handler;
  keyboard->context = context;
}

void grid_keyboard_push(GridKeyboard *keyboard, bool animated) {
  if (!keyboard) return;
  keyboard->accepted = false;
  keyboard->suppress_handlers = false;
  window_stack_push(keyboard->window, animated);
}

void grid_keyboard_pop(GridKeyboard *keyboard, bool animated) {
  if (!keyboard) return;
  keyboard->suppress_handlers = true;
  window_stack_remove(keyboard->window, animated);
}

void grid_keyboard_set_title(GridKeyboard *keyboard, const char *title) {
  if (!keyboard) return;
  keyboard->title = title;
  gk_mark_dirty(keyboard);
}

void grid_keyboard_set_placeholder(GridKeyboard *keyboard, const char *placeholder) {
  if (!keyboard) return;
  keyboard->placeholder = placeholder ? placeholder : "";
  gk_mark_dirty(keyboard);
}

void grid_keyboard_set_accent(GridKeyboard *keyboard, GColor accent) {
  if (!keyboard) return;
  keyboard->accent = accent;
  gk_mark_dirty(keyboard);
}

void grid_keyboard_set_text(GridKeyboard *keyboard, const char *text) {
  if (!keyboard) return;
  snprintf(keyboard->buffer, keyboard->buffer_size, "%s", text ? text : "");
  keyboard->length = strlen(keyboard->buffer);
  gk_mark_dirty(keyboard);
}

const char *grid_keyboard_get_text(const GridKeyboard *keyboard) {
  return keyboard ? keyboard->buffer : NULL;
}

void grid_keyboard_set_mode(GridKeyboard *keyboard, GridKeyboardMode mode) {
  if (!keyboard) return;
  keyboard->mode = (uint8_t) mode % 3;
  keyboard->fan_open = -1;
  gk_mark_dirty(keyboard);
}

void grid_keyboard_set_allow_empty(GridKeyboard *keyboard, bool allow_empty) {
  if (keyboard) keyboard->allow_empty = allow_empty;
}

void grid_keyboard_set_vibes(GridKeyboard *keyboard, bool enabled) {
  if (keyboard) keyboard->vibes = enabled;
}
