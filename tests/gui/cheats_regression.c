#define SDL_MAIN_HANDLED
#undef main
#define main cheats_emulator_main
#include "../../main.c"
#undef main
#include "../../microui/mui_integration.c"

#define CHECK(c, message) do { \
 if (!(c)){ fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); return 1; } \
} while (0)
static uint32 canvas[640U * 560U];

static void draw(void)
{
 memset(canvas, 0, sizeof(canvas));
 mui_render_overlay(canvas, 640U, 640U, 560U);
}

static int save_button(void)
{
 mu_Command *command = NULL;
 int x = -1, y = -1;
 draw();
 while (mu_next_command(mui_state.ctx, &command)){
  if (command->type == MU_COMMAND_TEXT && !strcmp(command->text.str, "SAVE")){
   x = command->text.pos.x + 3; y = command->text.pos.y + 6; break;
  }
 }
 CHECK(x >= 0, "Save button visible");
 mu_input_mousemove(mui_state.ctx, x, y); draw(); draw();
 mu_input_mousedown(mui_state.ctx, x, y, MU_MOUSE_LEFT); draw();
 mu_input_mouseup(mui_state.ctx, x, y, MU_MOUSE_LEFT); draw();
 return 0;
}

int main(void)
{
 cheat_entry_t entry = {0};
 appcfg_defaults(&main_cfg); main_headless = TRUE;
 cheats_set_current_path("cheats.cfg");
 mainui_set_cheats_autoloadsave(FALSE);
 CHECK(guicore_init(GUICORE_HEADLESS, "Cheats test"), "offscreen GUI");
 mui_state.ctx = calloc(1, sizeof(*mui_state.ctx));
 CHECK(mui_state.ctx, "GUI allocation");
 mu_init(mui_state.ctx);
 mui_state.ctx->text_width = mui_text_width;
 mui_state.ctx->text_height = mui_text_height;
 mui_filedialog_init(&mui_rom_dialog);
 mui_filedialog_init(&mui_dir_dialog);
 mui_filedialog_init(&mui_cfg_file_dialog);
 entry.used = TRUE; entry.enabled = TRUE; entry.width_bytes = 1;
 entry.addr = 42U; entry.value = 21U;
 strcpy(entry.desc, "Save edited value");
 mainui_set_cheat_entry(0U, &entry);
 mui_window_open(mui_state.ctx, MUI_WIN_VIEW_CHEATS); mui_refresh_visibility();
 draw();
 strcpy(mui_cheat_value, "77");
 CHECK(!save_button(), "click Save");
 CHECK(mainui_get_cheat_entry(0U, &entry) && entry.value == 77U, "Save applies selected editor values");
 CHECK(!mainui_get_cheat_dirty(), "Save clears dirty state");
 entry.value = 1U; mainui_set_cheat_entry(0U, &entry);
 mainui_cheats_reload_now();
 CHECK(mainui_get_cheat_entry(0U, &entry) && entry.value == 77U, "saved editor values reload from disk");
 entry.width_bytes = 4U; entry.signed_value = TRUE;
 entry.value = 0xFEDCBA98U; entry.compare_used = TRUE; entry.compare = 0x12345678U;
 mainui_set_cheat_entry(1U, &entry);
 entry.width_bytes = 2U; entry.signed_value = FALSE;
 entry.value = 0xABCDU; entry.compare = 0x6789U;
 mainui_set_cheat_entry(2U, &entry);
 CHECK(cheats_save_file(NULL), "save wide entries");
 cheats_clear_all();
 CHECK(cheats_load_file(cheats_get_current_path()), "reload wide entries");
 CHECK(mainui_get_cheat_entry(1U, &entry) && entry.width_bytes == 4U && entry.signed_value &&
       entry.value == 0xFEDCBA98U && entry.compare_used && entry.compare == 0x12345678U,
       "32-bit signed values and comparisons survive round trip");
 CHECK(mainui_get_cheat_entry(2U, &entry) && entry.width_bytes == 2U && !entry.signed_value &&
       entry.value == 0xABCDU && entry.compare == 0x6789U, "16-bit values survive round trip");
 {
  FILE* legacy = fopen("legacy.cfg", "w");
  CHECK(legacy, "create legacy fixture");
  fputs("Cheat0Enabled=1\nCheat0Addr=0x02A\nCheat0Value=0xA5\n", legacy);
  CHECK(!fclose(legacy), "close legacy fixture");
  CHECK(cheats_load_file("legacy.cfg"), "legacy file loads");
  CHECK(mainui_get_cheat_entry(0U, &entry) && entry.width_bytes == 1U && !entry.signed_value &&
        entry.value == 0xA5U, "legacy entries default to unsigned bytes");
 }
#if defined(TARGET_LINUX)
 mainui_set_cheat_entry(0U, &entry);
 CHECK(!cheats_save_file("/dev/full") && cheats_is_dirty(), "failed writes retain dirty state");
#endif
 free(mui_state.ctx); mui_state.ctx = NULL;
 guicore_quit();
 puts("Cheat editor Save regression passed.");
 return 0;
}
