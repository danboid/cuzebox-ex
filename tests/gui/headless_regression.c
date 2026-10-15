#define SDL_MAIN_HANDLED
#undef main
#define main headless_emulator_main
#include "../../main.c"
#undef main
#include "../../audio.c"
#include "../../guicore.c"

#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); return 1; } } while (0)

int main(void)
{
 uint8 samples[262], source[262], saved[262];
 sint16 left[256], right[256];
 uint32* pixels;
 auint w, h, i, frame;
 audio_scope_status_t status;
 /* Invalid drivers prove this path does not even initialize host devices. */
 SDL_setenv("SDL_VIDEODRIVER", "invalid-headless-test", 1);
 SDL_setenv("SDL_AUDIODRIVER", "invalid-headless-test", 1);
 CHECK(guicore_init(GUICORE_HEADLESS, "Headless regression"), "offscreen initialization");
 CHECK(guicore_window == NULL && guicore_renderer == NULL && guicore_texture == NULL, "no host display objects");
 CHECK(guicore_getpalette()[255U] != 0U, "offscreen palette initialized");
 guicore_getpixbuf()[0] = guicore_packrgb(10, 20, 30);
 guicore_update(FALSE);
 CHECK(guicore_capture_pixels(&pixels, &w, &h), "offscreen screenshot on request");
 CHECK(w == 640U && h == 560U && (pixels[0] & 0xFFFFFFU) == 0x0A141EU, "capture retains actual pixels and dimensions");
 free(pixels);
 CHECK(guicore_init(GUICORE_HEADLESS | GUICORE_SMALL, "Reconfigure"), "offscreen display reconfiguration");
 CHECK(guicore_setfullscreen(TRUE), "fullscreen preference cannot open headless window");
 audio_set_headless(TRUE);
 CHECK(audio_init(), "offline audio initialization");
 audio_reset();
 for (i = 0; i < 262; ++i){ samples[i] = (i & 16U) ? 200U : 60U; }
 /* More than a ring's worth must not stop accepting new source samples. */
 for (frame = 0; frame < 50; ++frame){
  samples[261] = (uint8)frame;
  audio_sendframe(samples, 262U);
 }
 CHECK(audio_scope_copy_source(source, 262U) == 262U && !memcmp(source, samples, 262U), "source ring stays current without callback");
 memcpy(saved, source, sizeof(saved));
 audio_sendframe(NULL, 262U);
 audio_scope_copy_source(source, 262U);
 CHECK(!memcmp(source, saved, sizeof(saved)), "pause retains guest audio");
 audio_scope_get_status(&status);
 CHECK(status.output_frames_total == 0ULL, "output DSP skipped while capture disabled");
 audio_scope_output_enable(TRUE);
 for (frame = 0; frame < 10; ++frame){ audio_sendframe(samples, 262U); }
 audio_scope_get_status(&status);
 CHECK(status.output_frames_total == (2620ULL * status.output_rate) / 15734ULL, "output scope follows emulated sample time");
 CHECK(audio_scope_copy_output(left, right, 256U) == 256U, "offline output scope readable");
 CHECK(status.output_peak_left > 0U, "offline output contains guest sound");
 audio_scope_output_enable(FALSE);
 audio_sendframe(samples, 262U);
 audio_scope_get_status(&status);
 CHECK(status.output_frames_total == 0ULL, "disabling output capture stops DSP");
 CHECK(audio_dev == 0U, "no audio device opened");
 CHECK(!(SDL_WasInit(0U) & (SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER)), "no video/audio/controller subsystem initialized");
 audio_quit(); guicore_quit();
 puts("PASS: no host devices, lazy screenshots/output audio, fresh source ring, paused audio retention");
 return 0;
}
