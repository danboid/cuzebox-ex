#define SDL_MAIN_HANDLED
#undef main
#define main shortcuts_emulator_main
#include "../../main.c"
#undef main
#include "../../microui/mui_integration.c"
#include "../../ginput.c"
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); return 1; } } while (0)
static uint32 canvas[640U * 560U];
static void draw(void){ memset(canvas, 0, sizeof(canvas)); mui_render_overlay(canvas, 640U, 640U, 560U); }
static int click(char const* text)
{
 mu_Command* cmd = NULL; int x = -1, y = -1;
 draw();
 while (mu_next_command(mui_state.ctx, &cmd)){
  if (cmd->type == MU_COMMAND_TEXT && strcmp(cmd->text.str, text) == 0){ x = cmd->text.pos.x + 3; y = cmd->text.pos.y + 6; break; }
 }
 CHECK(x >= 0, text);
 mu_input_mousemove(mui_state.ctx, x, y); draw(); draw();
 mu_input_mousedown(mui_state.ctx, x, y, MU_MOUSE_LEFT); draw();
 mu_input_mouseup(mui_state.ctx, x, y, MU_MOUSE_LEFT); draw();
 return 0;
}
static void pump(void){ SDL_Event e; SDL_PumpEvents(); while (SDL_PollEvent(&e)){ ginput_sendevent(&e); } }
static void pad(auint button, boole down)
{
 SDL_JoystickSetVirtualButton(SDL_GameControllerGetJoystick(ginput_gamectr[7]), button, down);
 SDL_JoystickUpdate(); pump();
}
int main(void)
{
 shortcut_binding_t b[SHORTCUT_COUNT]; uint32 held = 0U; auint suppress, a, other;
 SDL_Event key = {0}; app_config_t loaded;
 shortcuts_defaults(b);
 CHECK(!shortcuts_conflict(b, &a, &other), "defaults have no duplicate bindings");
 CHECK(shortcuts_key_action(b, SDLK_F6, 0) == SHORTCUT_MOUSE, "default mouse shortcut");
 CHECK(shortcuts_key_action(b, SDLK_F6, SHORTCUT_MOD_SHIFT) == SHORTCUT_LIGHTGUN, "shift action is distinct");
 CHECK(shortcuts_key_action(b, SDLK_F6, SHORTCUT_MOD_CTRL) == -1, "unassigned modifier combination does not execute a plain shortcut");
 CHECK(shortcuts_modifiers(KMOD_LCTRL | KMOD_RSHIFT | KMOD_CAPS | KMOD_NUM) == (SHORTCUT_MOD_CTRL | SHORTCUT_MOD_SHIFT), "modifier sides normalized; locks ignored");
 b[SHORTCUT_PAUSE].buttons = 1U << 10;
 b[SHORTCUT_FRAME].buttons = (1U << 10) | (1U << 11);
 CHECK(shortcuts_pad_action(b, 7, b[SHORTCUT_FRAME].buttons, &held, &suppress) == SHORTCUT_FRAME, "most specific controller chord wins");
 CHECK(shortcuts_pad_action(b, 7, b[SHORTCUT_FRAME].buttons, &held, &suppress) == -1, "held chord does not repeat");
 CHECK(shortcuts_pad_action(b, 7, b[SHORTCUT_PAUSE].buttons, &held, &suppress) == -1, "partial chord release does not trigger smaller action");
 (void)shortcuts_pad_action(b, 7, 0, &held, &suppress);
 CHECK(shortcuts_pad_action(b, 7, b[SHORTCUT_PAUSE].buttons, &held, &suppress) == SHORTCUT_PAUSE, "fresh press works after release");
 b[SHORTCUT_FRAME].buttons = b[SHORTCUT_PAUSE].buttons;
 CHECK(shortcuts_conflict(b, &a, &other), "duplicate controller chord rejected");
 b[SHORTCUT_FRAME].controller = 1U; b[SHORTCUT_PAUSE].controller = 7U;
 CHECK(!shortcuts_conflict(b, &a, &other), "different specific controllers may share a chord");
 SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
 CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0, "SDL dummy input init");
 SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
 int device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
 CHECK(device >= 0, "virtual gamepad");
 char guid[33], mapping[1024];
 SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(device), guid, sizeof(guid));
 snprintf(mapping, sizeof(mapping), "%s,Shortcut Test Pad,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,leftstick:b7,rightstick:b8,leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,", guid);
 CHECK(SDL_GameControllerAddMapping(mapping) >= 0, "SDL controller map");
 ginput_init(); pump();
 /* Put the open controller in slot eight to cover non-legacy controller routing. */
 ginput_gamectr[7] = ginput_gamectr[0]; ginput_gamectr_id[7] = ginput_gamectr_id[0]; ginput_gamectr[0] = NULL;
 appcfg_defaults(&main_cfg); cu_ctr_reset(); cu_vdev_reset(); cu_multitap_reset();
 auint v = cu_vdev_create(CU_VDEV_TYPE_PAD16, "Pad eight");
 CHECK(cu_vdev_set_binding(v, 0, CU_VDEV_HOST_GAMECONTROLLER, 7, CU_VDEV_BIND_BUTTONS), "virtual input binding");
 main_headless = TRUE;
 CHECK(guicore_init(GUICORE_HEADLESS, "Shortcuts test"), "offscreen canvas");
 mui_state.ctx = calloc(1U, sizeof(*mui_state.ctx)); CHECK(mui_state.ctx, "allocate GUI");
 mu_init(mui_state.ctx); mui_state.ctx->text_width = mui_text_width; mui_state.ctx->text_height = mui_text_height;
 mui_filedialog_init(&mui_rom_dialog); mui_filedialog_init(&mui_dir_dialog); mui_filedialog_init(&mui_cfg_file_dialog);
 mui_window_open(mui_state.ctx, MUI_WIN_TOOLS); mui_refresh_visibility();
 CHECK(click("SHORTCUTS") == 0 && mui_state.show_shortcuts, "Tools opens real shortcut screen");
 mui_shortcuts.action = SHORTCUT_PAUSE;
 CHECK(click("F9") == 0 && mui_shortcuts.learning == 1, "keyboard capture button");
 int old_x = mui_state.mouse_x;
 pad(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, TRUE); mui_gamepad_tick();
 CHECK(mui_state.mouse_x != old_x && mui_shortcuts.learning == 1, "controller navigation can cancel keyboard capture on a handheld");
 pad(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, FALSE); mui_gamepad_tick();
 key.type = SDL_KEYDOWN; key.key.keysym.sym = SDLK_p; key.key.keysym.mod = KMOD_LCTRL;
 main_ispause = FALSE;
 CHECK(mui_handle_event(&key) == 1 && !main_ispause, "capture does not execute a shortcut");
 CHECK(mui_shortcuts.draft[SHORTCUT_PAUSE].key == SDLK_p && mui_shortcuts.draft[SHORTCUT_PAUSE].modifiers == SHORTCUT_MOD_CTRL, "Ctrl+P captured");
 CHECK(main_cfg.shortcuts[SHORTCUT_PAUSE].key == SDLK_F9, "draft leaves active binding unchanged");
 CHECK(click("CAPTURE PAD") == 0, "controller capture button");
 mui_shortcuts_poll();
 pad(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, TRUE); mui_shortcuts_poll();
 pad(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, TRUE); mui_shortcuts_poll();
 pad(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, FALSE); mui_shortcuts_poll();
 pad(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, FALSE); mui_shortcuts_poll();
 CHECK(mui_shortcuts.draft[SHORTCUT_PAUSE].buttons == ((1U << 10) | (1U << 11)), "controller chord captured across press/release events");
 CHECK(click("CANCEL") == 0, "cancel draft"); draw();
 CHECK(main_cfg.shortcuts[SHORTCUT_PAUSE].key == SDLK_F9 && !main_cfg.shortcuts[SHORTCUT_PAUSE].buttons, "cancel preserves live shortcuts");
 mui_window_open(mui_state.ctx, MUI_WIN_SHORTCUTS); mui_refresh_visibility(); draw();
 mui_shortcuts.action = SHORTCUT_PAUSE;
 mui_shortcuts.draft[SHORTCUT_PAUSE].key = SDLK_p; mui_shortcuts.draft[SHORTCUT_PAUSE].modifiers = SHORTCUT_MOD_CTRL;
 mui_shortcuts.draft[SHORTCUT_PAUSE].buttons = (1U << 10) | (1U << 11);
 mui_shortcuts.draft[SHORTCUT_PAUSE].controller = 7;
 mui_shortcuts.draft[SHORTCUT_FRAME] = mui_shortcuts.draft[SHORTCUT_PAUSE];
 CHECK(click("APPLY") == 0 && mui_state.show_shortcuts && strstr(mui_shortcuts.status, "Conflict"), "GUI blocks duplicate shortcuts");
 mui_shortcuts.draft[SHORTCUT_FRAME] = main_cfg.shortcuts[SHORTCUT_FRAME];
 draw();
 SDL_Surface* preview = SDL_CreateRGBSurfaceFrom(canvas, 640, 560, 32, 640 * 4, 0xFF0000, 0xFF00, 0xFF, 0xFF000000);
 CHECK(preview && SDL_SaveBMP(preview, "shortcuts.bmp") == 0, "GUI preview"); SDL_FreeSurface(preview);
 CHECK(click("APPLY") == 0 && !mui_state.show_shortcuts, "apply shortcut draft"); draw();
 mui_close_all_windows(); mui_refresh_visibility();
 main_ispause = FALSE; key.type = SDL_KEYDOWN; key.key.repeat = 0;
 CHECK(main_shortcut_key(&key) && main_ispause, "remapped keyboard shortcut executes");
 key.key.repeat = 1; CHECK(main_shortcut_key(&key) && main_ispause, "key repeat does not toggle again");
 key.type = SDL_KEYUP; CHECK(main_shortcut_key(&key), "shortcut release consumed even after modifier changes");
 key.type = SDL_KEYDOWN; key.key.repeat = 0; key.key.keysym.sym = SDLK_F9; key.key.keysym.mod = 0;
 CHECK(!main_shortcut_key(&key), "old keyboard binding removed");
 key.key.keysym.sym = SDLK_p; key.key.keysym.mod = KMOD_CTRL;
 cu_kbd_enabled = 1U; cu_kbd_bypassed = 0U;
 CHECK(!main_shortcut_key(&key), "guest keyboard capture takes precedence"); cu_kbd_enabled = 0U;
 /* A remapped GUI shortcut replaces the original chord and does not click
 ** through to the newly opened menu while its button remains held. */
 main_cfg.shortcuts[SHORTCUT_GUI].buttons = 1U << 14;
 main_headless = FALSE;
 main_shortcut_pad_tick();
 pad(SDL_CONTROLLER_BUTTON_BACK, TRUE); pad(SDL_CONTROLLER_BUTTON_START, TRUE); main_shortcut_pad_tick();
 CHECK(!mui_is_open(), "old controller GUI chord removed");
 pad(SDL_CONTROLLER_BUTTON_BACK, FALSE); pad(SDL_CONTROLLER_BUTTON_START, FALSE); main_shortcut_pad_tick();
 pad(SDL_CONTROLLER_BUTTON_LEFTSTICK, TRUE); main_shortcut_pad_tick();
 CHECK(mui_is_open() && mui_state.gamepad_slot == 7, "remapped GUI binding opens menu from controller eight");
 mui_gamepad_tick(); CHECK(mui_shortcut_menu_guard && mui_is_open(), "opening press is guarded until release");
 pad(SDL_CONTROLLER_BUTTON_LEFTSTICK, FALSE); main_shortcut_pad_tick(); mui_gamepad_tick();
 CHECK(!mui_shortcut_menu_guard, "GUI guard releases");
 pad(SDL_CONTROLLER_BUTTON_LEFTSTICK, TRUE); main_shortcut_pad_tick();
 CHECK(!mui_is_open(), "same remapped binding closes GUI");
 pad(SDL_CONTROLLER_BUTTON_LEFTSTICK, FALSE); main_shortcut_pad_tick();
 main_headless = FALSE; main_ispause = FALSE;
 main_shortcut_pad_tick(); /* seed latch after Apply */
 pad(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, TRUE); pad(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, TRUE);
 main_shortcut_pad_tick(); CHECK(main_ispause, "controller-eight shortcut dispatch");
 main_shortcut_pad_tick(); CHECK(main_ispause, "held controller shortcut does not repeat");
 uint32 packet; auint bits;
 CHECK(cu_vdev_build_low_active_packet(v, 0, &packet, &bits) && !(~packet & ((1U << 10) | (1U << 11))), "complete shortcut chord excluded from gameplay");
 pad(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, FALSE); pad(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, FALSE); main_shortcut_pad_tick();
 main_headless = TRUE;
 CHECK(appcfg_save(&main_cfg, "shortcuts.cfg") && appcfg_load_or_create(&loaded, "shortcuts.cfg"), "save / reload shortcut configuration");
 CHECK(memcmp(loaded.shortcuts, main_cfg.shortcuts, sizeof(loaded.shortcuts)) == 0, "all keyboard and controller shortcuts round-trip");
 FILE* legacy = fopen("legacy-shortcuts.cfg", "w"); CHECK(legacy, "legacy fixture");
 fputs("Version=1\n", legacy); fclose(legacy);
 CHECK(appcfg_load_or_create(&loaded, "legacy-shortcuts.cfg") && loaded.shortcuts[SHORTCUT_PAUSE].key == SDLK_F9, "old configs receive shortcut defaults");
 shortcut_binding_t defaults[SHORTCUT_COUNT]; shortcuts_defaults(defaults);
 CHECK(mainui_set_shortcuts(defaults), "restore defaults");
 CHECK(main_cfg.shortcuts[SHORTCUT_PAUSE].key == SDLK_F9, "default restored");
 ginput_quit(); mui_shutdown(); guicore_quit(); SDL_Quit();
 puts("PASS: shortcut defaults/modifiers, chord priority/edges, GUI capture/Apply/Cancel/conflicts, keyboard/guest routing, controller-eight isolation, persistence");
 return 0;
}
