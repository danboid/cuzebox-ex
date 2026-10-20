#define SDL_MAIN_HANDLED
#undef main
#define main controller_mapping_emulator_main
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
static void pump(void){ SDL_Event event; SDL_PumpEvents(); while (SDL_PollEvent(&event)){ ginput_sendevent(&event); } }
static void button(auint slot, auint key, boole pressed)
{
 SDL_JoystickSetVirtualButton(SDL_GameControllerGetJoystick(ginput_gamectr[slot]), (int)key, pressed);
 SDL_JoystickUpdate(); pump();
}
int main(void)
{
 auint ids[8], mask, bits; uint32 packet;
 app_config_t loaded;
 SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
 CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0, "SDL controller subsystem");
 SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
 for (auint i = 0; i < 8; ++i){
  char guid[33], mapping[1024];
  int device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
  CHECK(device >= 0, "attach virtual controller");
  SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(device), guid, sizeof(guid));
  snprintf(mapping, sizeof(mapping), "%s,Test Controller,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,leftstick:b7,rightstick:b8,leftshoulder:b9,rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,", guid);
  CHECK(SDL_GameControllerAddMapping(mapping) >= 0, "register virtual mapping");
 }
 ginput_init(); pump();
 CHECK(ginput_get_controller_slots() == 8, "eight host slots");
 cu_ctr_reset(); cu_vdev_reset(); cu_multitap_reset();
 for (auint i = 0; i < 8; ++i){
  CHECK(ginput_get_controller_present(i), "all eight controllers opened");
  ids[i] = cu_vdev_create(CU_VDEV_TYPE_PAD16, "Test pad");
  CHECK(cu_vdev_set_binding(ids[i], 0, CU_VDEV_HOST_GAMECONTROLLER, i, CU_VDEV_BIND_BUTTONS), "bind independent host");
  cu_multitap_set_slot_vdev(i / 4, i % 4, ids[i]);
 }
 cu_multitap_set_tap_present(0, TRUE); cu_multitap_set_tap_present(1, TRUE);
 for (auint i = 0; i < 8; ++i){
  button(i, SDL_CONTROLLER_BUTTON_A, TRUE);
  for (auint j = 0; j < 8; ++j){
   CHECK(ginput_host_get_controller_buttons(j, &mask), "read host");
   CHECK((mask & CU_CTR_SNES_M_B) == (i == j ? CU_CTR_SNES_M_B : 0U), "host input isolated");
   CHECK(cu_vdev_build_low_active_packet(ids[j], j / 4, &packet, &bits), "build emulated pad packet");
   CHECK(((~packet) & CU_CTR_SNES_M_B) == (i == j ? CU_CTR_SNES_M_B : 0U), "eight independently routed packets");
  }
  button(i, SDL_CONTROLLER_BUTTON_A, FALSE);
 }
 CHECK(ginput_host_get_controller_slot_from_instance(-12345) == GINPUT_MAX_CONTROLLERS, "unknown instance never becomes player one");
 button(7, SDL_CONTROLLER_BUTTON_LEFTSTICK, TRUE);
 CHECK(ginput_host_get_controller_buttons(7, &mask) && (mask & (1U << 14)), "stick click available for mapping");
 button(7, SDL_CONTROLLER_BUTTON_LEFTSTICK, FALSE);
 SDL_JoystickSetVirtualAxis(SDL_GameControllerGetJoystick(ginput_gamectr[7]), SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767); SDL_JoystickUpdate(); pump();
 CHECK(ginput_host_get_controller_buttons(7, &mask) && (mask & (1U << 12)), "trigger available for mapping");
 CHECK(cu_vdev_build_low_active_packet(ids[7], 1, &packet, &bits) && (packet & 0xFFFFU) == 0xFFFFU, "unassigned triggers leave SNES padding bits neutral");
 SDL_JoystickSetVirtualAxis(SDL_GameControllerGetJoystick(ginput_gamectr[7]), SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768); SDL_JoystickUpdate(); pump();
 {
  ginput_controller_options_t opts = {TRUE, 20U};
  SDL_JoystickSetVirtualAxis(SDL_GameControllerGetJoystick(ginput_gamectr[7]), SDL_CONTROLLER_AXIS_LEFTX, 10000); SDL_JoystickUpdate(); pump();
  CHECK(ginput_host_get_controller_buttons_ex(7, &mask, opts) && (mask & CU_CTR_SNES_M_RIGHT), "custom stick threshold");
  opts.left_stick = FALSE;
  CHECK(ginput_host_get_controller_buttons_ex(7, &mask, opts) && !(mask & CU_CTR_SNES_M_RIGHT), "stick disabled for gameplay");
  SDL_JoystickSetVirtualAxis(SDL_GameControllerGetJoystick(ginput_gamectr[7]), SDL_CONTROLLER_AXIS_LEFTX, 0); SDL_JoystickUpdate(); pump();
 }
 appcfg_defaults(&main_cfg); main_headless = TRUE;
 CHECK(guicore_init(GUICORE_HEADLESS, "Controller test"), "offscreen canvas");
 mui_state.ctx = calloc(1U, sizeof(*mui_state.ctx)); CHECK(mui_state.ctx, "GUI allocate");
 mu_init(mui_state.ctx); mui_state.ctx->text_width = mui_text_width; mui_state.ctx->text_height = mui_text_height;
 mui_filedialog_init(&mui_rom_dialog); mui_filedialog_init(&mui_dir_dialog); mui_filedialog_init(&mui_cfg_file_dialog);
 mui_window_open(mui_state.ctx, MUI_WIN_CONTROLLER_MAPPING); mui_refresh_visibility(); draw();
 CHECK(mui_padmap.active && ginput_mapping_capture_active(), "mapping window owns gameplay input");
 mui_padmap.destination = 7;
 button(7, SDL_CONTROLLER_BUTTON_A, TRUE);
 CHECK(cu_vdev_build_low_active_packet(ids[7], 1, &packet, &bits) && !(~packet & CU_CTR_SNES_M_B), "learning window suppresses virtual gameplay");
 mui_padmap_learn(8, FALSE); mui_padmap_poll();
 CHECK(mui_padmap.learn_target == 8, "held opening input is ignored");
 button(7, SDL_CONTROLLER_BUTTON_A, FALSE); mui_padmap_poll();
 button(7, SDL_CONTROLLER_BUTTON_X, TRUE); mui_padmap_poll();
 CHECK(mui_padmap.learn_target == -1 && mui_padmap_device()->binding[0].map_low16[1] == CU_CTR_SNES_M_A, "learn host button for target A");
 CHECK(cu_vdev_get(ids[7])->binding[0].map_low16[1] == CU_CTR_SNES_M_Y, "draft does not mutate live mapping");
 button(7, SDL_CONTROLLER_BUTTON_X, FALSE); mui_padmap_poll();
 CHECK(click("CANCEL") == 0, "actual Cancel click"); draw();
 CHECK(!ginput_mapping_capture_active() && cu_vdev_get(ids[7])->binding[0].map_low16[1] == CU_CTR_SNES_M_Y, "cancel discards draft and releases capture");
 mui_window_open(mui_state.ctx, MUI_WIN_CONTROLLER_MAPPING); mui_refresh_visibility(); draw();
 mui_padmap.destination = 7;
 CHECK(click("CONFIGURE ALL") == 0, "wizard button visible");
 CHECK(mui_padmap.learn_all && mui_padmap.learn_target == 0, "wizard starts with B");
 for (int t = 0; t < 12; ++t){
  button(7, SDL_CONTROLLER_BUTTON_A, FALSE); mui_padmap_poll();
  button(7, SDL_CONTROLLER_BUTTON_A, TRUE); mui_padmap_poll();
  CHECK(mui_padmap.learn_target == (t == 11 ? -1 : t + 1), "wizard advances one target per fresh press");
 }
 button(7, SDL_CONTROLLER_BUTTON_A, FALSE); mui_padmap_poll();
 CHECK(click("RESTORE DEFAULT BUTTONS") == 0, "restore selected draft buttons");
 CHECK(mui_padmap_device()->binding[0].map_low16[0] == CU_CTR_SNES_M_B, "default button mapping restored");
 mui_padmap_learn(8, FALSE);
 button(7, SDL_CONTROLLER_BUTTON_BACK, TRUE); button(7, SDL_CONTROLLER_BUTTON_START, TRUE); mui_gamepad_tick();
 CHECK(mui_padmap.learn_target == -1 && mui_window_is_open(mui_state.ctx, MUI_WIN_CONTROLLER_MAPPING), "menu chord cancels learning without closing setup");
 button(7, SDL_CONTROLLER_BUTTON_BACK, FALSE); button(7, SDL_CONTROLLER_BUTTON_START, FALSE); mui_padmap_poll();
 mui_padmap.options[7].left_stick = FALSE; mui_padmap.options[7].deadzone = 30U;
 /* Save an offscreen preview of the actual UI for visual inspection. */
 draw();
 SDL_Surface* preview = SDL_CreateRGBSurfaceFrom(canvas, 640, 560, 32, 640 * 4, 0xFF0000, 0xFF00, 0xFF, 0xFF000000);
 CHECK(preview && SDL_SaveBMP(preview, "controller-mapping.bmp") == 0, "save GUI preview"); SDL_FreeSurface(preview);
 CHECK(mui_padmap_duplicate(0), "duplicate assignment warning");
 CHECK(!mui_padmap_duplicate(7), "unique assignment accepted");
 mui_padmap_learn(8, FALSE); mui_padmap_poll(); button(7, SDL_CONTROLLER_BUTTON_X, TRUE); mui_padmap_poll(); button(7, SDL_CONTROLLER_BUTTON_X, FALSE); mui_padmap_poll();
 CHECK(click("PORT 2 MULTITAP") == 0 && !mui_padmap.topology.port[1].tap_present, "real tap checkbox");
 CHECK(mui_padmap.topology.port[1].slot_vdev[3] == ids[7], "hidden slot preserved");
 CHECK(click("PORT 2 MULTITAP") == 0, "re-enable tap");
 CHECK(click("APPLY") == 0, "actual Apply click"); draw();
 CHECK(cu_vdev_get(ids[7])->binding[0].map_low16[1] == CU_CTR_SNES_M_A, "apply installs mapping");
 button(7, SDL_CONTROLLER_BUTTON_X, TRUE);
 CHECK(cu_vdev_build_low_active_packet(ids[7], 1, &packet, &bits) && (~packet & CU_CTR_SNES_M_A) && !(~packet & CU_CTR_SNES_M_Y), "new mapping reaches emulated controller");
 button(7, SDL_CONTROLLER_BUTTON_X, FALSE);
 /* Explicitly clearing every button must survive config reload. */
 memset(cu_vdev_get_mut(ids[6])->binding[0].map_low16, 0, sizeof(cu_vdev_get_mut(ids[6])->binding[0].map_low16));
 main_cfg_capture_runtime();
 CHECK(appcfg_save(&main_cfg, "controllers.cfg") && appcfg_load_or_create(&loaded, "controllers.cfg"), "save and reload mappings");
 CHECK(loaded.vdev[ids[7]].binding[0].host_index == 7 && loaded.vdev[ids[7]].binding[0].map_low16[1] == CU_CTR_SNES_M_A, "controller eight mapping persists");
 CHECK(loaded.vdev[ids[6]].binding[0].map_low16[0] == 0, "fully cleared mapping stays cleared");
 CHECK(!loaded.controller_options[7].left_stick && loaded.controller_options[7].deadzone == 30U, "analog options persist");
 CHECK(strcmp(loaded.controller_identity[7], ginput_get_controller_identity(7)) == 0 && loaded.controller_identity[7][0], "host identity persists");
 CHECK(loaded.tap_slot_vdev[1][3] == ids[7] && loaded.tap_present[0] && loaded.tap_present[1], "dual multitap destinations persist");
 /* Remove and reconnect the eighth pad; earlier slots remain assigned. */
 cu_hap_init(TRUE);
 CHECK(!cu_hap_can_test_host(8), "haptic test rejects invalid controller slot");
#if SDL_VERSION_ATLEAST(2,0,18)
 CHECK(!cu_hap_can_test_host(7), "virtual pad without rumble disables haptic test");
#endif
 int remove_index = -1;
 for (int i = 0; i < SDL_NumJoysticks(); ++i){ if (SDL_JoystickGetDeviceInstanceID(i) == (SDL_JoystickID)ginput_gamectr_id[7]){ remove_index = i; break; } }
 CHECK(remove_index >= 0 && SDL_JoystickDetachVirtual(remove_index) == 0, "disconnect eighth pad"); pump();
 CHECK(!ginput_get_controller_present(7), "removal closes host handle");
 CHECK(!cu_hap_can_test_host(7), "disconnected pad disables haptic test");
 cu_hap_quit();
 int replacement = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0); pump();
 CHECK(replacement >= 0 && ginput_get_controller_present(7), "reconnect restores remembered slot");
 /* Remembered slots are used even when they differ from SDL enumeration order. */
 char identity[GINPUT_IDENTITY_CAP]; snprintf(identity, sizeof(identity), "%s", ginput_get_controller_identity(0));
 ginput_quit();
 for (auint i = 0; i < 8; ++i){ ginput_set_controller_identity(i, "reserved-other-device"); }
 ginput_set_controller_identity(7, identity); ginput_init();
 CHECK(ginput_get_controller_present(7) && !ginput_get_controller_present(0), "saved identity chooses slot instead of enumeration index");
 ginput_forget_disconnected_controllers();
 for (auint i = 0; i < 8; ++i){ CHECK(ginput_get_controller_present(i), "forget disconnected slots rescans all connected pads"); }
 button(7, SDL_CONTROLLER_BUTTON_BACK, TRUE); button(7, SDL_CONTROLLER_BUTTON_START, TRUE);
 main_headless = FALSE; main_shortcut_pad_tick(); main_headless = TRUE;
 CHECK(mui_gamepad_pick_slot() == 7, "eighth controller can claim GUI with menu chord");
 button(7, SDL_CONTROLLER_BUTTON_BACK, FALSE); button(7, SDL_CONTROLLER_BUTTON_START, FALSE);
 ginput_quit(); mui_shutdown(); guicore_quit(); SDL_Quit();
 puts("PASS: eight SDL controllers, isolated multitap packets, trigger/stick sources, GUI learning/Apply/Cancel, persistence, hotplug");
 return 0;
}
