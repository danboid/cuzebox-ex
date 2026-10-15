#ifndef SHORTCUTS_H
#define SHORTCUTS_H

#include "types.h"
#include "ginput.h"

/* Append actions to preserve numeric config keys across releases. */
typedef enum {
 SHORTCUT_QUIT, SHORTCUT_KEYBOARD, SHORTCUT_DISPLAY_SIZE, SHORTCUT_GAMEONLY,
 SHORTCUT_LIMITER, SHORTCUT_VIDEO, SHORTCUT_MOUSE, SHORTCUT_LIGHTGUN,
 SHORTCUT_MERGE, SHORTCUT_TAP1, SHORTCUT_LAYOUT, SHORTCUT_TAP2,
 SHORTCUT_PAUSE, SHORTCUT_NEXT1, SHORTCUT_FRAME, SHORTCUT_NEXT2,
 SHORTCUT_FULLSCREEN, SHORTCUT_ALLOC, SHORTCUT_GUI, SHORTCUT_RESET,
 SHORTCUT_SCREENSHOT, SHORTCUT_SAVE, SHORTCUT_LOAD, SHORTCUT_COUNT
} shortcut_action_t;

#define SHORTCUT_MOD_CTRL  1U
#define SHORTCUT_MOD_SHIFT 2U
#define SHORTCUT_MOD_ALT   4U
#define SHORTCUT_MOD_GUI   8U
#define SHORTCUT_ANY_CONTROLLER GINPUT_MAX_CONTROLLERS

typedef struct {
 sint32 key;
 auint modifiers;
 auint buttons;
 auint controller;
} shortcut_binding_t;

char const* shortcuts_name(auint action);
boole shortcuts_available(auint action);
auint shortcuts_modifiers(auint sdl_modifiers);
void shortcuts_defaults(shortcut_binding_t bindings[SHORTCUT_COUNT]);
void shortcuts_sanitize(shortcut_binding_t* binding);
int shortcuts_key_action(shortcut_binding_t const* bindings, sint32 key, auint modifiers);
/* Returns the most specific chord. held tracks all matching actions, so releasing
** part of a chord does not fire a smaller shortcut that was already held. */
int shortcuts_pad_action(shortcut_binding_t const* bindings, auint slot, auint buttons, uint32* held, auint* suppress);
boole shortcuts_conflict(shortcut_binding_t const* bindings, auint* first, auint* second);

#endif
