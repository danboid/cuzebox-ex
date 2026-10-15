/* Immediate-mode shortcut editor; changes remain a draft until Apply. */
static struct {
 boole active;
 shortcut_binding_t draft[SHORTCUT_COUNT];
 auint action;
 int learning; /* 0 idle, 1 keyboard, 2 controller */
 boole neutral;
 auint pad_slot;
 auint pad_buttons;
 boole guard;
 char status[192];
} mui_shortcuts;

static void mui_shortcuts_begin(void)
{
 memset(&mui_shortcuts, 0, sizeof(mui_shortcuts));
 mainui_get_shortcuts(mui_shortcuts.draft);
 mui_shortcuts.active = TRUE;
 snprintf(mui_shortcuts.status, sizeof(mui_shortcuts.status), "Choose an action, then capture a binding.");
}

static void mui_shortcuts_format_key(shortcut_binding_t const* b, char* out, size_t size)
{
 if (!b->key){ snprintf(out, size, "UNASSIGNED"); return; }
 snprintf(out, size, "%s%s%s%s%s", b->modifiers & SHORTCUT_MOD_CTRL ? "Ctrl+" : "",
  b->modifiers & SHORTCUT_MOD_SHIFT ? "Shift+" : "", b->modifiers & SHORTCUT_MOD_ALT ? "Alt+" : "",
  b->modifiers & SHORTCUT_MOD_GUI ? "GUI+" : "", SDL_GetKeyName((SDL_Keycode)b->key));
}

static void mui_shortcuts_format_pad(auint buttons, char* out, size_t size)
{
 static char const* const labels[16] = {"SOUTH", "WEST", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT", "EAST", "NORTH", "L1", "R1", "L2", "R2", "L3", "R3"};
 out[0] = 0;
 if (!buttons){ snprintf(out, size, "UNASSIGNED"); return; }
 for (auint i = 0; i < 16; ++i){
  if (buttons & (1U << i)){
   size_t used = strlen(out);
   if (used < size){ snprintf(out + used, size - used, "%s%s", used ? "+" : "", labels[i]); }
  }
 }
}

static int mui_shortcuts_key_event(SDL_Event const* event)
{
 if (!mui_shortcuts.active || !mui_state.show_shortcuts || !mui_shortcuts.learning){ return 0; }
 if (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP && event->type != SDL_TEXTINPUT){ return 0; }
 if (event->type != SDL_KEYDOWN || event->key.repeat){ return 1; }
 if (event->key.keysym.sym == SDLK_ESCAPE){ mui_shortcuts.learning = 0; mui_shortcuts.guard = TRUE; return 1; }
 if (mui_shortcuts.learning != 1){ return 1; }
 switch (event->key.keysym.sym){
  case SDLK_LSHIFT: case SDLK_RSHIFT: case SDLK_LCTRL: case SDLK_RCTRL:
  case SDLK_LALT: case SDLK_RALT: case SDLK_LGUI: case SDLK_RGUI: return 1;
  default: break;
 }
 shortcut_binding_t* b = &mui_shortcuts.draft[mui_shortcuts.action];
 b->key = event->key.keysym.sym; b->modifiers = shortcuts_modifiers(event->key.keysym.mod);
 mui_shortcuts.learning = 0; mui_shortcuts.guard = TRUE;
 snprintf(mui_shortcuts.status, sizeof(mui_shortcuts.status), "Keyboard binding captured. Apply to save.");
 return 1;
}

static void mui_shortcuts_poll(void)
{
 if (!mui_shortcuts.active){ return; }
 auint masks[GINPUT_MAX_CONTROLLERS] = {0U}, any = 0U;
 for (auint i = 0; i < GINPUT_MAX_CONTROLLERS; ++i){ (void)ginput_host_get_controller_buttons(i, &masks[i]); any |= masks[i]; }
 if (!any){ mui_shortcuts.guard = FALSE; }
 if (mui_shortcuts.learning != 2){ return; }
 shortcut_binding_t* b = &mui_shortcuts.draft[mui_shortcuts.action];
 auint raw = 0U;
 if (!mui_shortcuts.neutral){
  for (auint i = 0; i < GINPUT_MAX_CONTROLLERS; ++i){
   if (b->controller == SHORTCUT_ANY_CONTROLLER || b->controller == i){ raw |= masks[i]; }
  }
  if (!raw){ mui_shortcuts.neutral = TRUE; }
  return;
 }
 if (!mui_shortcuts.pad_buttons){
  for (auint i = 0; i < GINPUT_MAX_CONTROLLERS; ++i){
   if ((b->controller == SHORTCUT_ANY_CONTROLLER || b->controller == i) && masks[i]){
    mui_shortcuts.pad_slot = i; mui_shortcuts.pad_buttons = masks[i]; break;
   }
  }
  return;
 }
 if (!ginput_get_controller_present(mui_shortcuts.pad_slot)){
  mui_shortcuts.learning = 0; snprintf(mui_shortcuts.status, sizeof(mui_shortcuts.status), "Controller disconnected. Capture cancelled."); return;
 }
 raw = masks[mui_shortcuts.pad_slot];
 mui_shortcuts.pad_buttons |= raw;
 if (!raw){
  b->buttons = mui_shortcuts.pad_buttons;
  mui_shortcuts.learning = 0; mui_shortcuts.guard = TRUE;
  snprintf(mui_shortcuts.status, sizeof(mui_shortcuts.status), "Controller binding captured. Apply to save.");
 }
}

static void mui_build_shortcuts_window(mu_Context* ctx)
{
 char value[128];
 if (!mui_window_is_open(ctx, MUI_WIN_SHORTCUTS)){ mui_shortcuts.active = FALSE; mui_shortcuts.learning = 0; return; }
 if (!mui_shortcuts.active){ mui_shortcuts_begin(); }
 if (!mui_window_begin(ctx, MUI_WIN_SHORTCUTS)){ return; }
 mu_layout_row(ctx, 2, (int[]){220, -1}, 24);
 if (mu_button(ctx, "APPLY")){
  auint a, b;
  if (mui_shortcuts.learning){ snprintf(mui_shortcuts.status, sizeof(mui_shortcuts.status), "Finish or cancel capture first."); }
  else if (shortcuts_conflict(mui_shortcuts.draft, &a, &b)){
   snprintf(mui_shortcuts.status, sizeof(mui_shortcuts.status), "Conflict: %s and %s. Clear or change one binding.", shortcuts_name(a), shortcuts_name(b));
  }else if (mainui_set_shortcuts(mui_shortcuts.draft)){ mui_window_close(ctx, MUI_WIN_SHORTCUTS); }
 }
 if (mu_button(ctx, "CANCEL")){ mui_window_close(ctx, MUI_WIN_SHORTCUTS); }
 char const* names[SHORTCUT_COUNT];
 for (auint i = 0; i < SHORTCUT_COUNT; ++i){ names[i] = shortcuts_name(i); }
 if (mui_labeled_choice_popup(ctx, "ACTION", "shortcut_action", names[mui_shortcuts.action], names, SHORTCUT_COUNT, &mui_shortcuts.action, 90, 376)){ mui_shortcuts.learning = 0; }
 shortcut_binding_t* b = &mui_shortcuts.draft[mui_shortcuts.action];
 mu_layout_row(ctx, 1, (int[]){-1}, 20);
 mu_label(ctx, shortcuts_available(mui_shortcuts.action) ? "KEYBOARD COMBINATION" : "ACTION DISABLED IN THIS BUILD");
 mui_shortcuts_format_key(b, value, sizeof(value));
 mu_push_id(ctx, "keyboard_binding", 16);
 mu_layout_row(ctx, 2, (int[]){-96, 84}, 24);
 if (mu_button(ctx, mui_shortcuts.learning == 1 ? "PRESS KEY..." : value)){ mui_shortcuts.learning = 1; }
 if (mu_button(ctx, "CLEAR")){ b->key = 0; b->modifiers = 0U; mui_shortcuts.learning = 0; }
 mu_pop_id(ctx);
 mu_layout_row(ctx, 1, (int[]){-1}, 20); mu_label(ctx, "CONTROLLER COMBINATION");
 {
  static char const* const controllers[] = {"PAD 1", "PAD 2", "PAD 3", "PAD 4", "PAD 5", "PAD 6", "PAD 7", "PAD 8", "ANY PAD"};
  if (mui_labeled_choice_popup(ctx, "DEVICE", "shortcut_device", controllers[b->controller], controllers, 9U, &b->controller, 90, 180)){ mui_shortcuts.learning = 0; }
 }
 mui_shortcuts_format_pad(b->buttons, value, sizeof(value));
 mu_push_id(ctx, "controller_binding", 18);
 mu_layout_row(ctx, 2, (int[]){-96, 84}, 24);
 if (mu_button(ctx, mui_shortcuts.learning == 2 ? "PRESS CHORD..." : "CAPTURE PAD")){
  mui_shortcuts.learning = 2; mui_shortcuts.neutral = FALSE; mui_shortcuts.pad_buttons = 0U;
 }
 if (mu_button(ctx, "CLEAR")){ b->buttons = 0U; mui_shortcuts.learning = 0; }
 mu_pop_id(ctx);
 mu_layout_row(ctx, 1, (int[]){-1}, 0); mu_text(ctx, value);
 mu_layout_row(ctx, 1, (int[]){-1}, 24);
 if (mu_button(ctx, "CANCEL CAPTURE")){ mui_shortcuts.learning = 0; mui_shortcuts.guard = TRUE; }
 if (mu_button(ctx, "RESTORE THIS ACTION")){
  shortcut_binding_t defaults[SHORTCUT_COUNT]; shortcuts_defaults(defaults); *b = defaults[mui_shortcuts.action]; mui_shortcuts.learning = 0;
 }
 if (mu_button(ctx, "RESTORE ALL DEFAULTS")){ shortcuts_defaults(mui_shortcuts.draft); mui_shortcuts.learning = 0; }
 mu_layout_row(ctx, 1, (int[]){-1}, 0);
 mu_text(ctx, mui_shortcuts.learning == 2 ? "Release controls, hold the desired buttons together, then release to capture. Escape cancels." : mui_shortcuts.status);
 mu_text(ctx, "Controller shortcuts use physical positions, independently of gameplay mappings. Prefer chords to avoid gameplay overlap.");
 mu_text(ctx, "Guest keyboard capture takes priority. Ctrl+F1 remains its emergency escape. Escape still closes menus.");
 mui_window_end(ctx, MUI_WIN_SHORTCUTS);
}
