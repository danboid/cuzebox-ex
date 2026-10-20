#define SDL_MAIN_HANDLED
#undef main
#define main interlace_emulator_main
#include "../../main.c"
#undef main
#include "../../cu_avr.c"
#include "../../video_interlace.h"
#include <assert.h>
static uint32 rgb(uint8 const* bmp, auint w, auint h, auint x, auint y)
{
 auint stride=(w*3U+3U)&~3U;
 uint8 const* p=bmp+54U+(h-1U-y)*stride+x*3U;
 return (p[2]<<16)|(p[1]<<8)|p[0];
}
static void fill(uint32 color)
{
 uint32* p=guicore_getpixbuf();
 uint32 native=guicore_packrgb(color>>16,(color>>8)&255,color&255);
 for(unsigned i=0;i<640U*280U;i++) p[i]=native;
}
int main(void)
{
 /* Renderer HSync intervals can resemble the legacy vertical shortcuts.
  * With interlace enabled neither may reposition an active raster. */
 cu_avr_set_interlace(TRUE);
 video_fields.active=TRUE;
 cpu_state.iors[CU_IO_DDRB]=1;
 cpu_state.iors[CU_IO_PORTB]=0;
 video_pulsectr=100;video_prise=0;video_pedge=1616;cpu_state.cycle=1752;
 cu_avr_write_io(CU_IO_PORTB,1);
 assert(video_pulsectr==101);
 cpu_state.iors[CU_IO_PORTB]=0;
 video_pulsectr=266;video_prise=0;video_pedge=842;cpu_state.cycle=978;
 cu_avr_write_io(CU_IO_PORTB,1);
 assert(video_pulsectr==267);
 cu_avr_set_interlace(FALSE);
 video_interlace_t s;
 uint32_t t=0xffff0000U;
 video_interlace_reset(&s);
 for(unsigned i=0;i<8;i++){
  video_interlace_field(&s,t+=1820U*(262U+(i&1U)),i&1U);
  assert(s.active==(i>=2));
  if(i) assert(s.completed==((i-1)&1U));
 }
 video_interlace_field(&s,t+=1820U*262U,1); assert(!s.active);
 video_interlace_reset(&s); assert(!s.active);
 /* An isolated short pulse followed by ordinary HSync must not acquire. */
 video_interlace_reset(&s);
 assert(video_interlace_sync(&s,68,842,0)==0);
 for(unsigned i=0;i<300;i++) assert(video_interlace_sync(&s,136,1820,i*1820)==0);
 assert(!s.active);
 for(unsigned field=0;field<4;field++){
  unsigned short_entry=field&1U;
  assert(video_interlace_sync(&s,68,short_entry?842:1752,0)==0);
  for(unsigned i=1;i<6;i++)
   assert(video_interlace_sync(&s,68,910,0)==((i==2)?254:0));
  assert(video_interlace_sync(&s,774,1616,0)==0);
  for(unsigned i=1;i<6;i++) assert(video_interlace_sync(&s,774,910,0)==0);
  assert(video_interlace_sync(&s,68,204,0)==0);
  for(unsigned i=1;i<6;i++) assert(video_interlace_sync(&s,68,910,0)==0);
  assert(video_interlace_sync(&s,136,short_entry?1888:978,field*1820U*262U)==270);
  assert(s.active==(field>=2));
 }
 SDL_setenv("SDL_VIDEODRIVER","dummy",1);
 SDL_SetHint(SDL_HINT_RENDER_DRIVER,"software");
 assert(guicore_init(GUICORE_NOVSYNC|GUICORE_GAMEONLY,"Interlace test"));
 for(unsigned path=2;path<=3;path++){
 guicore_set_render_path(path);
 for(unsigned presentation=0;presentation<2;presentation++){
  guicore_set_field_presentation(presentation);
  fill(0xFF0000);guicore_commit_field(TRUE,0);
  fill(0x00FF00);guicore_commit_field(TRUE,1);
  uint8* bmp;auint bytes,w,h;
  assert(mainui_capture_screenshot_bmp(&bmp,&bytes,&w,&h));
  assert(w==620 && h==456);
  assert(rgb(bmp,w,h,100,100)==(presentation?0xFF0000:0));
  assert(rgb(bmp,w,h,100,101)==0x00FF00);
  free(bmp);
  guicore_commit_field(FALSE,0);
  assert(mainui_capture_screenshot_bmp(&bmp,&bytes,&w,&h));
  assert(rgb(bmp,w,h,100,100)==0x00FF00 && rgb(bmp,w,h,100,101)==0x00FF00);
  free(bmp);
 }
 }
 appcfg_defaults(&main_cfg); assert(main_cfg.interlace_mode==0 && main_cfg.interlace_presentation==0);
 main_rom_is_uze=TRUE;main_rom_head.video=1;main_interlace_apply_policy();assert(cu_avr_interlace_enabled());
 mainui_set_interlace_mode(1);assert(!cu_avr_interlace_enabled());
 mainui_set_interlace_mode(0);assert(cu_avr_interlace_enabled());
 main_rom_is_uze=FALSE;main_interlace_apply_policy();assert(!cu_avr_interlace_enabled());
 mainui_set_interlace_mode(2);assert(cu_avr_interlace_enabled());
 mainui_set_interlace_mode(3);assert(cu_avr_interlace_enabled());
 mainui_set_interlace_presentation(2);
 assert(appcfg_save(&main_cfg,"interlace.cfg"));
 app_config_t loaded;appcfg_defaults(&loaded);assert(appcfg_load_or_create(&loaded,"interlace.cfg"));
 assert(loaded.interlace_mode==3 && loaded.interlace_presentation==2);
 puts("PASS: field detection, wrap, loss, fields/weave pixels, disable, ROM overrides, persistence");
 return 0;
}
