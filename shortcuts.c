#include "shortcuts.h"
#include <string.h>

static char const* const names[SHORTCUT_COUNT] = {
 "QUIT", "KEYBOARD PASSTHROUGH OFF", "TOGGLE WINDOW SIZE", "TOGGLE GAME ONLY",
 "TOGGLE SPEED LIMIT", "TOGGLE VIDEO RECORDING", "CYCLE MOUSE SENSITIVITY", "TOGGLE P1 LIGHTGUN",
 "TOGGLE FRAME MERGE", "TOGGLE P1 MULTITAP", "TOGGLE KEYBOARD LAYOUT", "TOGGLE P2 MULTITAP",
 "PAUSE / RESUME", "NEXT P1 TAP SLOT", "ADVANCE ONE FRAME", "NEXT P2 TAP SLOT",
 "TOGGLE FULLSCREEN", "TOGGLE PLAYER ALLOCATION", "OPEN / CLOSE GUI", "RESET ROM",
 "SAVE SCREENSHOT", "SAVE CURRENT STATE SLOT", "LOAD CURRENT STATE SLOT"
};

char const* shortcuts_name(auint action){ return action < SHORTCUT_COUNT ? names[action] : "UNKNOWN"; }

boole shortcuts_available(auint action)
{
 if (action >= SHORTCUT_COUNT){ return FALSE; }
#ifdef TARGET_EMSCRIPTEN
 if (action == SHORTCUT_SCREENSHOT || action == SHORTCUT_FULLSCREEN || action == SHORTCUT_QUIT){ return FALSE; }
#endif
#ifndef ENABLE_VCAP
 if (action == SHORTCUT_VIDEO){ return FALSE; }
#endif
#ifndef ENABLE_MICROUI
 if (action == SHORTCUT_GUI){ return FALSE; }
#endif
 return TRUE;
}

auint shortcuts_modifiers(auint mod)
{
 auint out = 0U;
 if (mod & KMOD_CTRL){ out |= SHORTCUT_MOD_CTRL; }
 if (mod & KMOD_SHIFT){ out |= SHORTCUT_MOD_SHIFT; }
 if (mod & KMOD_ALT){ out |= SHORTCUT_MOD_ALT; }
#ifdef USE_SDL1
 if (mod & KMOD_META){ out |= SHORTCUT_MOD_GUI; }
#else
 if (mod & KMOD_GUI){ out |= SHORTCUT_MOD_GUI; }
#endif
 return out;
}

void shortcuts_defaults(shortcut_binding_t b[SHORTCUT_COUNT])
{
 static sint32 const keys[SHORTCUT_COUNT] = {
  SDLK_ESCAPE, SDLK_F1, SDLK_F2, SDLK_F3, SDLK_F4, SDLK_F5, SDLK_F6, SDLK_F6,
  SDLK_F7, SDLK_F7, SDLK_F8, SDLK_F8, SDLK_F9, SDLK_F9, SDLK_F10, SDLK_F10,
  SDLK_F11, SDLK_F12, 0, 0, 0, 0, 0
 };
 memset(b, 0, sizeof(*b) * SHORTCUT_COUNT);
 for (auint i = 0; i < SHORTCUT_COUNT; ++i){ b[i].key = keys[i]; b[i].controller = SHORTCUT_ANY_CONTROLLER; }
 b[SHORTCUT_LIGHTGUN].modifiers = b[SHORTCUT_TAP1].modifiers = b[SHORTCUT_TAP2].modifiers =
 b[SHORTCUT_NEXT1].modifiers = b[SHORTCUT_NEXT2].modifiers = SHORTCUT_MOD_SHIFT;
 /* Source bits follow the normalized SNES positions, independent of gameplay remaps. */
#ifdef TARGET_RGB30
 b[SHORTCUT_GUI].buttons = (1U << 3) | (1U << 10) | (1U << 11);
 b[SHORTCUT_QUIT].buttons = (1U << 2) | (1U << 3);
#else
 b[SHORTCUT_GUI].buttons = (1U << 2) | (1U << 3);
#endif
}

void shortcuts_sanitize(shortcut_binding_t* b)
{
 if (b->key < 0){ b->key = 0; }
 b->modifiers &= 15U;
 b->buttons &= 0xFFFFU;
 if (b->controller > SHORTCUT_ANY_CONTROLLER){ b->controller = SHORTCUT_ANY_CONTROLLER; }
 if (!b->key){ b->modifiers = 0U; }
}

int shortcuts_key_action(shortcut_binding_t const* b, sint32 key, auint mods)
{
 if (!key){ return -1; }
 for (auint i = 0; i < SHORTCUT_COUNT; ++i){
  if (shortcuts_available(i) && b[i].key == key && b[i].modifiers == mods){ return (int)i; }
 }
 return -1;
}

int shortcuts_pad_action(shortcut_binding_t const* b, auint slot, auint buttons, uint32* held, auint* suppress)
{
 uint32 matched = 0U;
 int best = -1; auint best_count = 0U;
 *suppress = 0U;
 for (auint i = 0; i < SHORTCUT_COUNT; ++i){
  if (!shortcuts_available(i) || !b[i].buttons || (b[i].controller != SHORTCUT_ANY_CONTROLLER && b[i].controller != slot)){ continue; }
  if ((buttons & b[i].buttons) == b[i].buttons){
   auint count = 0U;
   for (auint mask = b[i].buttons; mask; mask &= mask - 1U){ ++count; }
   matched |= (uint32)1U << i; *suppress |= b[i].buttons;
   if (count > best_count){ best_count = count; best = (int)i; }
  }
 }
 int result = best >= 0 && ((*held & ((uint32)1U << best)) == 0U) ? best : -1;
 *held = matched;
 return result;
}

boole shortcuts_conflict(shortcut_binding_t const* b, auint* first, auint* second)
{
 for (auint i = 0; i < SHORTCUT_COUNT; ++i){
  for (auint j = i + 1U; j < SHORTCUT_COUNT; ++j){
   boole key = b[i].key && b[i].key == b[j].key && b[i].modifiers == b[j].modifiers;
   boole pad = b[i].buttons && b[i].buttons == b[j].buttons &&
    (b[i].controller == SHORTCUT_ANY_CONTROLLER || b[j].controller == SHORTCUT_ANY_CONTROLLER || b[i].controller == b[j].controller);
   if (key || pad){ *first = i; *second = j; return TRUE; }
  }
 }
 return FALSE;
}
