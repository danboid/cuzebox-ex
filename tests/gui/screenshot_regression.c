#define SDL_MAIN_HANDLED
#undef main
#define main screenshot_emulator_main
#include "../../main.c"
#undef main

#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); return 1; } } while (0)

static uint32 bmp_rgb(uint8 const* bmp, auint w, auint h, auint x, auint y)
{
 auint row = ((w * 3U) + 3U) & ~3U;
 uint8 const* p = bmp + 54U + (h - 1U - y) * row + x * 3U;
 return ((uint32)p[2] << 16) | ((uint32)p[1] << 8) | p[0];
}

int main(void)
{
 static auint const rotations[] = {0U, JAMMA_ROTATE_90, JAMMA_ROTATE_180, JAMMA_ROTATE_270};
 /* Which source quadrant appears at each output corner after clockwise rotation. */
 static auint const corners[4][4] = {{0,1,2,3}, {2,0,3,1}, {3,2,1,0}, {1,3,0,2}};
 static uint32 const colors[] = {0xE02030U, 0x30D040U, 0x2040E0U, 0xD0C020U};
 auint layout, rot, flip, x, y, count = 0;
 SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
 SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
 for (layout = 0; layout < 4; ++layout){
  auint flags = GUICORE_NOVSYNC | ((layout & 1U) ? GUICORE_SMALL : 0U) |
                ((layout & 2U) ? GUICORE_GAMEONLY : 0U);
  auint scale = (layout & 1U) ? 1U : 2U;
  auint sw = (layout & 2U) ? 310U : 320U;
  auint sh = (layout & 2U) ? 228U : 246U;
  auint sx = (layout & 2U) ? 5U : 0U;
  auint expected_w = sw * scale;
  auint expected_h = ((layout & 2U) ? 228U : 280U) * scale;
  uint32* raw;
  CHECK(guicore_init(flags, "Screenshot regression"), "initialize dummy display");
  guicore_set_filter_pre_mode(0U);
  guicore_set_filter_scale_mode(0U);
  guicore_set_filter_crt_mode(0U);
  raw = guicore_getpixbuf();
  for (y = 0; y < 280; ++y){
   for (x = 0; x < 320; ++x){
    uint32 rgb = (y < 19U) ? 0x123456U : 0x654321U;
    if (y >= 19U && y < 19U + sh && x >= sx && x < sx + sw){
     rgb = colors[((y - 19U >= sh / 2U) ? 2U : 0U) + ((x - sx >= sw / 2U) ? 1U : 0U)];
    }
    raw[y * 640U + x * 2U] = raw[y * 640U + x * 2U + 1U] =
      guicore_packrgb(rgb >> 16, (rgb >> 8) & 255U, rgb & 255U);
   }
  }
  for (rot = 0; rot < 4; ++rot){
   for (flip = 0; flip < 4; ++flip){
    uint8* bmp;
    auint bytes, w, h, left, right, top, bottom, i;
    SDL_Surface* file;
    SDL_Surface* converted;
    guicore_set_jamma(rotations[rot] | ((flip & 1U) ? JAMMA_FLIP_H : 0U) |
                            ((flip & 2U) ? JAMMA_FLIP_V : 0U));
    CHECK(mainui_capture_screenshot_bmp(&bmp, &bytes, &w, &h), "capture in-memory BMP");
    CHECK(w == expected_w && h == expected_h, "capture uses rendered dimensions");
    CHECK(bytes == 54U + (((w * 3U) + 3U) & ~3U) * h, "BMP allocation covers every row");
    CHECK(main_save_screenshot_to_path("capture.bmp"), "save file BMP");
    file = SDL_LoadBMP("capture.bmp");
    CHECK(file && file->w == (int)w && file->h == (int)h, "file BMP dimensions match");
    converted = SDL_ConvertSurfaceFormat(file, SDL_PIXELFORMAT_ARGB8888, 0U);
    CHECK(converted, "decode saved BMP");
    for (y = 0; y < h; ++y){
     uint32 const* row = (uint32*)((uint8*)converted->pixels + y * converted->pitch);
     for (x = 0; x < w; ++x){
      CHECK((row[x] & 0xFFFFFFU) == bmp_rgb(bmp, w, h, x, y), "file and API pixels agree through final row");
     }
    }
    /* Sample well inside the four quadrants, including portrait letterboxing. */
    left = w * 2U / 5U; right = w * 3U / 5U;
    top = ((layout & 2U) ? 0U : 19U * scale) + sh * scale / 4U;
    bottom = top + sh * scale / 2U;
    for (i = 0; i < 4; ++i){
     auint transformed_corner = i ^ flip;
     CHECK(bmp_rgb(bmp, w, h, (i & 1U) ? right : left, (i & 2U) ? bottom : top) ==
           colors[corners[rot][transformed_corner]], "JAMMA rotation and mirrors orient game pixels");
    }
    if (!(layout & 2U)){
     CHECK(bmp_rgb(bmp, w, h, 0U, 0U) == 0x123456U, "top HUD remains upright");
     CHECK(bmp_rgb(bmp, w, h, w - 1U, h - 1U) == 0x654321U, "bottom HUD and final row remain intact");
    }
    SDL_FreeSurface(converted); SDL_FreeSurface(file); free(bmp);
    ++count;
   }
  }
 }
 remove("capture.bmp");
 guicore_quit();
 printf("PASS: %u screenshot layouts, JAMMA rotations/mirrors, upright HUD, file/API pixel agreement\n", count);
 return 0;
}
