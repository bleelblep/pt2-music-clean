// Smallest useful grid_keyboard app: a menu-less screen that shows the last
// thing you typed and reopens the keyboard on SELECT.
//
// Build it by copying grid_keyboard.c / grid_keyboard.h and this file into a
// new project's src/c/ and running `pebble build`.

#include <pebble.h>

#include "grid_keyboard.h"

static Window *s_window;
static TextLayer *s_text_layer;
static char s_query[64];

static void on_text_entered(const char *text, void *context) {
  text_layer_set_text(s_text_layer, text);
}

static void on_cancelled(void *context) {
  text_layer_set_text(s_text_layer, "(cancelled)");
}

static void open_keyboard(ClickRecognizerRef recognizer, void *context) {
  // The one-call path: it pushes its own window and cleans itself up.
  GridKeyboard *keyboard = grid_keyboard_show(s_query, sizeof(s_query), on_text_entered, NULL);
  if (!keyboard) return;
  // Everything past this point is optional - the line above is already a
  // working keyboard.
  grid_keyboard_set_handlers(keyboard, on_text_entered, on_cancelled, NULL);
  grid_keyboard_set_title(keyboard, "SEARCH");
  grid_keyboard_set_accent(keyboard, GColorFolly);
}

static void click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, open_keyboard);
}

static void window_load(Window *window) {
  GRect bounds = layer_get_bounds(window_get_root_layer(window));
  s_text_layer = text_layer_create(GRect(6, bounds.size.h / 2 - 30, bounds.size.w - 12, 60));
  text_layer_set_font(s_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_text_layer, GTextAlignmentCenter);
  text_layer_set_text(s_text_layer, "SELECT to type");
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(s_text_layer));
}

static void window_unload(Window *window) {
  text_layer_destroy(s_text_layer);
}

int main(void) {
  s_window = window_create();
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
  app_event_loop();
  window_destroy(s_window);
  return 0;
}
