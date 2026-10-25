#define SDL_MAIN_HANDLED
#undef main
#define main video_settings_emulator_main
#include "../../main.c"
#undef main
#include "../../microui/mui_integration.c"
#include "../../frame.c"
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); return 1; } } while (0)
static uint32 canvas[640U * 560U];
static void draw(unsigned height)
{
 memset(canvas, 0, sizeof(canvas));
 mui_render_overlay(canvas, 640U, 640U, height);
}
static int visible(char const *label)
{
 mu_Command *cmd = NULL;
 while (mu_next_command(mui_state.ctx, &cmd)){
  if (cmd->type == MU_COMMAND_TEXT && !strcmp(cmd->text.str, label)) return 1;
 }
 return 0;
}
static void screenshot(char const *path, unsigned height)
{
 SDL_Surface *s = SDL_CreateRGBSurfaceFrom(canvas, 640, height, 32, 640*4,
                                         0x00ff0000, 0x0000ff00, 0x000000ff, 0);
 if (s){ SDL_SaveBMP(s, path); SDL_FreeSurface(s); }
}
int main(void)
{
 mu_Container *cnt;
 appcfg_defaults(&main_cfg); main_headless = TRUE;
 CHECK(!mainui_get_area_weighted(), "sampling defaults off");
 main_rom_is_uze=TRUE; main_rom_head.video=VIDEO_AREA_WEIGHTED | 3U;
 main_interlace_apply_policy();
 CHECK(mainui_get_area_weighted() && frame_area_weighted, "video hint enables area sampling");
 mainui_set_area_weighted(FALSE);
 CHECK(!mainui_get_area_weighted() && !frame_area_weighted, "manual mode overrides hint immediately");
 CHECK(appcfg_save(&main_cfg,"sampling.cfg"), "save sampling override");
 appcfg_defaults(&main_cfg);
 CHECK(appcfg_load_or_create(&main_cfg,"sampling.cfg") && main_cfg.area_weighted_mode==1U,"override persists");
 mainui_set_area_weighted_mode(0U);
 CHECK(mainui_get_area_weighted(), "restore hint");
 main_rom_is_uze=FALSE; main_interlace_apply_policy();
 CHECK(!frame_area_weighted, "HEX clears ROM hint");
 main_rom_is_uze=TRUE; main_rom_head.video=3U; main_interlace_apply_policy();
 CHECK(!frame_area_weighted, "interlace-only hint stays unfiltered");
 {
  uint8 source[ROW_ACTIVE_CYCLES]; uint32 output[ROW_PIXELS], palette[256];
  auint x,t;
  for(x=0;x<256;x++) palette[x]=0xFF000000U | x*0x010101U;
  for(x=0;x<ROW_ACTIVE_CYCLES;x++) source[x]=(uint8)x;
  guicore_set_render_path(RENDER_PATH_STAGED);
  frame_line32_raw(output,source,0,ROW_PIXELS,palette);
  for(x=0;x<ROW_PIXELS;x++) CHECK(output[x]==palette[source[frame_raw_xmap[x]]],"disabled sampling preserves nearest output");
  frame_init_blend_map();
  for(x=0;x<ROW_PIXELS;x++){
   auint sum=0; for(t=0;t<RAW_TAPS;t++) sum+=frame_blend_w[x][t];
   CHECK(sum==256U,"area weights preserve brightness");
  }
  memset(source,255,sizeof(source)); frame_set_area_weighted(TRUE);
  frame_line32_raw(output,source,0,ROW_PIXELS,palette);
  for(x=0;x<ROW_PIXELS;x++) CHECK(output[x]==0xFFFFFFFFU,"area sampling preserves white including edges");
  frame_set_area_weighted(FALSE);
 }
 main_cfg.tv_sync_pll = TRUE;
 CHECK(guicore_init(GUICORE_HEADLESS, "Video settings test"), "offscreen canvas");
 mui_state.ctx = calloc(1U, sizeof(*mui_state.ctx)); CHECK(mui_state.ctx, "GUI allocate");
 mu_init(mui_state.ctx);
 mui_state.ctx->text_width = mui_text_width;
 mui_state.ctx->text_height = mui_text_height;
 mui_filedialog_init(&mui_rom_dialog); mui_filedialog_init(&mui_dir_dialog);
 mui_filedialog_init(&mui_cfg_file_dialog);
 mui_open_config_page(mui_state.ctx, 0); mui_refresh_visibility();
 draw(560); draw(560);
 CHECK(visible("SAMPLING"), "sampling dropdown visible");
 CHECK(visible("PLL RECOVERY %") && visible("PLL SKEW %"), "both PLL controls visible");
 screenshot("video-settings.bmp", 560);
 CHECK(!(mui_window_descs[MUI_WIN_CFG_VIDEO].opts & MU_OPT_NOSCROLL), "video page permits scrolling");
 draw(360); draw(360);
 cnt = mui_find_container(mui_state.ctx, "VIDEO");
 CHECK(cnt && cnt->rect.y + cnt->rect.h <= 352, "window fits short viewport");
 CHECK(cnt->content_size.y > cnt->body.h, "small viewport exercises overflow");
 mu_input_mousemove(mui_state.ctx, cnt->body.x+10, cnt->body.y+10);
 draw(360); draw(360);
 mu_input_scroll(mui_state.ctx, 0, 1000);
 draw(360); draw(360);
 CHECK(cnt->scroll.y > 0, "overflow scrolls");
 CHECK(visible("SAVE"), "bottom controls accessible after scrolling");
 screenshot("video-settings-small.bmp", 360);
 guicore_quit();
 puts("PASS: PLL controls, viewport fit, scrolling and bottom controls");
 return 0;
}
