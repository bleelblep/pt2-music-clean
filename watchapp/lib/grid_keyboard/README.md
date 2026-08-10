# grid_keyboard

A drop-in 3×3 swipe keyboard for Pebble, packaged the way `tertiary_text` is:
two files, one call, no state left in your app.

```c
#include "grid_keyboard.h"

static char s_query[64];

static void on_done(const char *text, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "typed: %s", text);
}

grid_keyboard_show(s_query, sizeof(s_query), on_done, NULL);
```

That's the whole integration. The keyboard creates and pushes its own window,
subscribes to its own clicks and touches, calls you back with the finished
string, and destroys itself when its window leaves the stack.

## Install

Copy `grid_keyboard.c` and `grid_keyboard.h` into your project's `src/c/`. The
stock Pebble `wscript` globs `src/c/**/*.c`, so nothing else needs configuring.

To keep it out of the app until you actually use it, leave it here in `lib/`
and add the folder to the build explicitly:

```python
ctx.pbl_build(source=ctx.path.ant_glob(['src/c/**/*.c', 'lib/grid_keyboard/*.c']),
              target=app_elf, bin_type='app', includes=['lib/grid_keyboard'])
```

`example/main.c` is a complete demo app: copy it plus the two library files
into a fresh project's `src/c/` and run `pebble build`.

## Layout

Nine keys, three characters each — the alphabet in reading order, so there is
nothing to memorise:

```
 abc   def   ghi
 jkl   m n   opq
 rst   uvw   xyz
```

**Tap** a key for its middle letter (`b`, `e`, `h`, `k`, space, `p`, `s`, `v`,
`y` — space being the one you reach for most). **Swipe** from a key toward one
of its neighbours for the outer two: while your finger is down, the three
characters spread onto the keys you would flick at, so the destination square
*is* the label. You aim at where the letter appears, not at a remembered
compass direction, and you don't have to land on it — the gesture is matched by
angle.

The mode badge cycles `abc` → `ABC` → `123`. Number mode puts the digits on the
taps, phone-keypad style, with punctuation on the same swipe directions. Zero
lives on the `8` key, swiped up-right; there is no tenth square for it.

## Buttons

Fully usable without touching the screen, which is what keeps it working on
watches that have no touchscreen at all:

| Button | Action |
| --- | --- |
| UP / DOWN | move the focus ring between keys |
| SELECT | open the focused key, spreading its three characters onto UP / SELECT / DOWN |
| SELECT (again) | pick the one on that button |
| BACK | close an open key, else delete, else leave |
| SELECT (hold) | accept |
| UP (hold) | cycle abc / ABC / 123 |
| DOWN (hold) | clear the field |
| BACK (hold) | show the cheat sheet |

Touch and buttons stay live at the same time; using one just dismisses the
other's highlight.

## Screen badges

Three tap targets sit along the bottom of the text field: the mode badge
(`abc`), a `?` that flashes the cheat sheet for a few seconds, and `GO`, which
accepts. `GO` greys out on an empty field unless you called
`grid_keyboard_set_allow_empty(kb, true)`.

## API

The rest of the surface is optional and documented in `grid_keyboard.h`:
`grid_keyboard_create` / `_push` / `_pop` / `_destroy` when you want to keep one
keyboard around and reuse it, plus `_set_title`, `_set_placeholder`,
`_set_accent`, `_set_text`, `_get_text`, `_set_mode`, `_set_allow_empty` and
`_set_vibes`.

Two things worth knowing:

- **The buffer is yours.** The keyboard never allocates or grows it; typing
  simply stops at `buffer_size - 1`. Whatever is in the buffer when you push
  becomes the starting text, so resuming a half-finished query is free.
- **The window is popped before your result handler runs**, so the handler is
  free to push its own window without ending up underneath the keyboard.

## Portability

Geometry is computed from the layer bounds rather than hard-coded, so the
keyboard fits whatever display it lands on. Touch support is compiled in on
Emery (Pebble Time 2) and compiles out cleanly everywhere else — call
`grid_keyboard_touch_available()` if your UI wants to say which model is
available. Both configurations build warning-free under `-Wall -Wextra`.

## Relationship to the in-app keyboard

This is a standalone repackaging of the grid keyboard in `watchapp/src/c/main.c`
(the `s_keyboard_pt2` path). The swipe map and the angle-matching are carried
over unchanged, so the muscle memory is identical. What differs:

- Tapping a key types its middle letter. In `main.c` a tap types nothing except
  on the centre key, so entry there is swipe-only.
- Buttons can type. In `main.c` the grid path indexes a table with a selection
  that never moves, so button entry is effectively dead and SELECT appends a
  stray `\n`.
- Number mode has 27 characters instead of 9, on the same fan directions.
- Layout comes from the layer bounds instead of fixed 200×228 coordinates.

`main.c` is untouched. Migrating it means deleting the `s_keyboard_pt2`
branches from `draw_keyboard`, `keyboard_choose`, `keyboard_back_level`, the
`pt2_*` touch helpers and the four click handlers, then calling
`grid_keyboard_show` from `open_keyboard` with `submit_search_query` as the
result handler.
