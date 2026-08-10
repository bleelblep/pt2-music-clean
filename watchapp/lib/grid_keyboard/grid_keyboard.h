// grid_keyboard - a drop-in 3x3 swipe keyboard for Pebble.
//
// Same idea as tertiary_text: you hand it a buffer, it pushes its own window,
// and it calls you back with the finished string. It owns its window, layer,
// click config and touch subscription; the host app owns nothing but the buffer.
//
//   static char s_text[64];
//
//   static void on_done(const char *text, void *context) {
//     APP_LOG(APP_LOG_LEVEL_INFO, "typed: %s", text);
//   }
//
//   grid_keyboard_show(s_text, sizeof(s_text), on_done, NULL);
//
// Entry is touch-first (tap a cell for its middle letter, swipe toward one of
// its neighbours for the outer two), with a full button fallback so the
// keyboard stays usable on hardware without a touchscreen.
//
// See README.md for the layout, the gesture map and the button model.

#pragma once

#include <pebble.h>

typedef struct GridKeyboard GridKeyboard;

// Called when the user accepts the text (long SELECT, or the GO badge).
// `text` points at the caller's buffer and stays valid until the buffer is
// reused; copy it if you need to keep it. The keyboard window is popped before
// the handler runs, so it is safe to push another window from here.
typedef void (*GridKeyboardResultHandler)(const char *text, void *context);

// Called when the user backs out without accepting. Optional.
typedef void (*GridKeyboardCancelHandler)(void *context);

typedef enum {
  GridKeyboardModeLower = 0,   // abc
  GridKeyboardModeUpper = 1,   // ABC
  GridKeyboardModeSymbols = 2, // 123 and punctuation
} GridKeyboardMode;

// ---------------------------------------------------------------------------
// The one-call path
// ---------------------------------------------------------------------------

// Creates a keyboard, pushes it, and destroys it automatically once its window
// leaves the stack. `buffer` must outlive the keyboard; its current contents
// are used as the starting text. Returns NULL if allocation failed.
//
// This is all most callers need. Use the create/push path below only when you
// want to set a title, an accent colour or a starting mode first, or when you
// want to keep one keyboard around and reuse it.
GridKeyboard *grid_keyboard_show(char *buffer, size_t buffer_size,
                                 GridKeyboardResultHandler handler, void *context);

// ---------------------------------------------------------------------------
// The explicit path
// ---------------------------------------------------------------------------

// Creates a keyboard bound to `buffer`. Nothing is shown until you push it.
// The keyboard never grows the buffer: typing stops at buffer_size - 1 chars.
GridKeyboard *grid_keyboard_create(char *buffer, size_t buffer_size);

// Tears down the window and frees the keyboard. Safe to call with NULL, and
// safe to call while the window is still on the stack (it is removed first).
// Do not call this from inside a result/cancel handler of the same keyboard -
// use grid_keyboard_show() if you want that lifetime handled for you.
void grid_keyboard_destroy(GridKeyboard *keyboard);

void grid_keyboard_set_handlers(GridKeyboard *keyboard,
                                GridKeyboardResultHandler result_handler,
                                GridKeyboardCancelHandler cancel_handler,
                                void *context);

// Pushes the keyboard's window onto the stack.
void grid_keyboard_push(GridKeyboard *keyboard, bool animated);

// Removes the keyboard's window from the stack without firing either handler.
void grid_keyboard_pop(GridKeyboard *keyboard, bool animated);

// ---------------------------------------------------------------------------
// Appearance and behaviour (all optional, all safe before or after push)
// ---------------------------------------------------------------------------

// Small caption above the text field. NULL or "" hides the caption row and
// gives the height back to the grid. Not copied - pass a literal or a string
// that outlives the keyboard.
void grid_keyboard_set_title(GridKeyboard *keyboard, const char *title);

// Shown in place of the text when the buffer is empty.
// Defaults to "Start typing...". Not copied.
void grid_keyboard_set_placeholder(GridKeyboard *keyboard, const char *placeholder);

// Highlight colour for the pressed cell and the mode badge.
// Defaults to GColorVividCerulean on colour watches, GColorBlack otherwise.
void grid_keyboard_set_accent(GridKeyboard *keyboard, GColor accent);

// Replaces the buffer contents. Truncated to fit.
void grid_keyboard_set_text(GridKeyboard *keyboard, const char *text);

const char *grid_keyboard_get_text(const GridKeyboard *keyboard);

void grid_keyboard_set_mode(GridKeyboard *keyboard, GridKeyboardMode mode);

// When false (the default), accepting an empty buffer is refused with a short
// buzz instead of calling the result handler.
void grid_keyboard_set_allow_empty(GridKeyboard *keyboard, bool allow_empty);

// Haptic feedback on each keypress. Defaults to true.
void grid_keyboard_set_vibes(GridKeyboard *keyboard, bool enabled);

// True on builds where the grid can be driven by touch. On everything else the
// button model is the only way in, which is worth saying out loud in your UI.
bool grid_keyboard_touch_available(void);
