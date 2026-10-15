/* Exercise production dialog layout, scrolling and outside-click hit testing
 * without a native window. Include the integration to inspect its real
 * dismissal predicate; main.c supplies its normal application dependencies. */
#define SDL_MAIN_HANDLED
#undef main
#define main gui_emulator_main
#include "../../main.c"
#undef main
#include "../../microui/mui_integration.c"

#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); return 1; } } while (0)

static int test_width(mu_Font font, char const* str, int len)
{
 (void)font;
 return (len < 0 ? (int)strlen(str) : len) * 8;
}

static int test_height(mu_Font font){ (void)font; return 16; }

static void draw_dialog(mui_filedialog_t* dlg)
{
 mu_begin(mui_state.ctx);
 (void)mui_filedialog_draw(mui_state.ctx, dlg, mu_rect(20, 32, 540, 472));
 mu_end(mui_state.ctx);
 mui_refresh_visibility();
}

static mu_Container* list_panel(mui_filedialog_t* dlg)
{
 mu_Container* panel;
 mu_push_id(mui_state.ctx, dlg->title, (int)strlen(dlg->title));
 panel = mu_get_container(mui_state.ctx, "##files");
 mu_pop_id(mui_state.ctx);
 return panel;
}

static void click(mui_filedialog_t* dlg, int x, int y)
{
 SDL_Event event = {0};
 event.type = SDL_MOUSEMOTION;
 event.motion.x = x;
 event.motion.y = y;
 (void)mui_handle_event(&event);
 draw_dialog(dlg); /* First establish the hover root, then the control. */
 draw_dialog(dlg);
 event.type = SDL_MOUSEBUTTONDOWN;
 event.button.button = SDL_BUTTON_LEFT;
 event.button.x = x;
 event.button.y = y;
 (void)mui_handle_event(&event);
 draw_dialog(dlg);
 event.type = SDL_MOUSEBUTTONUP;
 (void)mui_handle_event(&event);
 draw_dialog(dlg);
}

static void make_dir(char const* path)
{
#ifdef _WIN32
 (void)_mkdir(path);
#else
 (void)mkdir(path, 0777);
#endif
}

static int exercise_dialog(mui_filedialog_t* dlg, char const* title)
{
 mu_Context* ctx = mui_state.ctx;
 mu_Container* panel;
 mu_Id drag_focus;
 int x, y, first_scroll, step;
 mui_filedialog_open(dlg, title, "fixture/child", ".uze;.hex");
 draw_dialog(dlg);
 panel = list_panel(dlg);
 CHECK(panel->rect.w == panel->body.w, "small directory needs no scrollbar");

 /* The child has only '..'. Reproduce navigation into a large parent. */
 click(dlg, panel->body.x + 40, panel->body.y + ctx->style->padding + 10);
 CHECK(dlg->entry_count >= 200, "parent directory was loaded");
 CHECK(panel->rect.w - panel->body.w == ctx->style->scrollbar_size, "large parent has scrollbar immediately");
 CHECK(panel->content_size.y > 200 * 20, "scroll extent includes the entire directory");
 CHECK(panel->scroll.y == 0, "directory navigation resets scroll");
 x = panel->rect.x + panel->rect.w - ctx->style->scrollbar_size / 2;
 y = panel->rect.y + panel->rect.h - ctx->style->scrollbar_size / 2;
 CHECK(mui_point_in_any_ui(x, y), "named dialog scrollbar is inside the GUI");
 CHECK(!mui_point_in_any_ui(600, 520), "click outside dialog remains outside the GUI");

 click(dlg, x, y);
 CHECK(dlg->open && panel->scroll.y > 0, "scroll arrow works without closing dialog");
 first_scroll = panel->scroll.y;
 click(dlg, x, panel->rect.y + panel->rect.h - ctx->style->scrollbar_size - 4);
 CHECK(panel->scroll.y > first_scroll, "scroll track pages through the list");

 panel->scroll.y = 0;
 draw_dialog(dlg);
 y = panel->rect.y + ctx->style->scrollbar_size + 2;
 mu_input_mousemove(ctx, x, y);
 draw_dialog(dlg);
 mu_input_mousedown(ctx, x, y, MU_MOUSE_LEFT);
 draw_dialog(dlg);
 drag_focus = ctx->focus;
 CHECK(drag_focus != 0U, "scrollbar thumb acquires focus");
 mu_input_mousemove(ctx, x, y + 35);
 draw_dialog(dlg);
 first_scroll = panel->scroll.y;
 CHECK(first_scroll > 0 && ctx->focus == drag_focus, "thumb drag survives layout changes");
 mu_input_mousemove(ctx, x, y + 70);
 draw_dialog(dlg);
 CHECK(panel->scroll.y > first_scroll && ctx->focus == drag_focus, "thumb keeps dragging over multiple frames");
 mu_input_mouseup(ctx, x, y + 70, MU_MOUSE_LEFT);
 draw_dialog(dlg);

 panel->scroll.y = 0;
 mu_input_mousemove(ctx, panel->body.x + 40, panel->body.y + 40);
 draw_dialog(dlg);
 mu_input_scroll(ctx, 0, 90);
 draw_dialog(dlg);
 CHECK(panel->scroll.y == 90, "mouse wheel scrolls the full list");

 panel->scroll.y = panel->content_size.y + ctx->style->padding * 2 - panel->body.h;
 draw_dialog(dlg);
 step = 20 + ctx->style->spacing;
 y = panel->body.y + ctx->style->padding + (dlg->entry_count - 1) * step - panel->scroll.y + 10;
 click(dlg, panel->body.x + 40, y);
 CHECK(strstr(dlg->selected_path, "file199.uze") != NULL, "last file can be selected");
 CHECK(dlg->open, "single file click does not dismiss picker");

 mui_filedialog_open(dlg, title, "fixture/child", ".uze;.hex");
 draw_dialog(dlg);
 CHECK(panel->scroll.y == 0 && panel->rect.w == panel->body.w, "return to small directory removes stale scrollbar");
 {
  SDL_Event outside = {0};
  outside.type = SDL_MOUSEBUTTONDOWN;
  outside.button.button = SDL_BUTTON_LEFT;
  outside.button.x = 600;
  outside.button.y = 520;
  CHECK(mui_handle_event(&outside) == 1 && !dlg->open, "outside click still dismisses the picker");
  mu_input_mouseup(ctx, 600, 520, MU_MOUSE_LEFT);
 }
 return 0;
}

int main(void)
{
 int i;
 char path[128];
 make_dir("fixture");
 make_dir("fixture/child");
 for (i = 0; i < 200; ++i){
  FILE* f;
  snprintf(path, sizeof(path), "fixture/file%03d.uze", i);
  f = fopen(path, "wb");
  CHECK(f != NULL, "create directory fixture");
  fclose(f);
 }
 mui_state.ctx = calloc(1U, sizeof(*mui_state.ctx));
 CHECK(mui_state.ctx != NULL, "allocate MicroUI context");
 mu_init(mui_state.ctx);
 mui_state.ctx->text_width = test_width;
 mui_state.ctx->text_height = test_height;
 mui_filedialog_init(&mui_rom_dialog);
 mui_filedialog_init(&mui_dir_dialog);
 mui_filedialog_init(&mui_cfg_file_dialog);
 CHECK(exercise_dialog(&mui_rom_dialog, "ROM LOAD") == 0, "ROM picker");
 CHECK(exercise_dialog(&mui_dir_dialog, "CHOOSE SD DIRECTORY") == 0, "directory picker hit testing");
 CHECK(exercise_dialog(&mui_cfg_file_dialog, "SELECT BOOTLOADER OVERRIDE") == 0, "config picker hit testing");
 mui_filedialog_destroy(&mui_rom_dialog);
 mui_filedialog_destroy(&mui_dir_dialog);
 mui_filedialog_destroy(&mui_cfg_file_dialog);
 free(mui_state.ctx);
 mui_state.ctx = NULL;
 puts("PASS: directory transitions, named dialog hit testing, arrows, track, multi-frame thumb dragging, wheel, last-file selection");
 return 0;
}
