#define SDL_MAIN_HANDLED
#undef main
#define main storage_emulator_main
#include "../main.c"
#undef main
#include "../cu_vfat.h"
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"Storage regression failed at %d: %s\n",__LINE__,#x); return 1; } } while(0)
static boole command(char const *text)
{
 char input[512], reply[4096]; snprintf(input,sizeof(input),"%s",text);
 logical_test_command(input+8,reply,sizeof(reply));
 if(!strstr(reply,"\"ok\":1")) { fprintf(stderr,"%s: %s\n",text,reply); return FALSE; }
 return TRUE;
}
int main(void)
{
 appcfg_defaults(&main_cfg); main_headless=TRUE;
#ifndef _WIN32
 /* Default/reset socket state must not close stdin (or an API socket reusing 0). */
 int sentinel=open("/dev/null",O_RDONLY); CHECK(sentinel>=0);
 CHECK(dup2(sentinel,0)==0); if(sentinel) close(sentinel);
 cu_esp_reset_factory(); cu_esp_net_cleanup();
 CHECK(fcntl(0,F_GETFD)!=-1);
#endif
 CHECK(guicore_init(GUICORE_HEADLESS,"Storage test"));
 audio_set_headless(TRUE); CHECK(audio_init());
 cu_avr_reset(); cu_spir_set_size(2); cu_spisd_reset(0); cu_vfat_reset();
 cu_multitap_reset(); cu_ctr_reset(); cu_vdev_reset();
 mainui_debug_set_paused(TRUE);
 FILE *f=fopen("fixture.img","wb"); CHECK(f);
 for(unsigned i=0;i<1024;i++) fputc(i&255,f);
 fclose(f);
 CHECK(command("LOGICAL CONFIG 0 3 1 1000 0"));
 CHECK(command("LOGICAL INPUT 0 100"));
 CHECK(command("LOGICAL SD LOAD fixture.img"));
 CHECK(cu_vfat_read(700)==(700&255)); cu_vfat_write(700,42);
 CHECK(command("LOGICAL CHECKPOINT SAVE storage.lct"));
 CHECK(command("LOGICAL CHECKPOINT TAKE 0")); cu_vfat_write(700,99);
 CHECK(command("LOGICAL CHECKPOINT RESTORE 0")); CHECK(cu_vfat_read(700)==42);
 cu_vfat_write(700,88);
 CHECK(command("LOGICAL CHECKPOINT LOAD storage.lct")); CHECK(cu_vfat_read(700)==42);
 CHECK(cu_vfat_read(1024)==255); cu_vfat_write(1024,1); CHECK(cu_vfat_read(1024)==255);
 CHECK(command("LOGICAL SD SAVE final.img"));
 f=fopen("fixture.img","rb"); CHECK(f); CHECK(!fseek(f,700,SEEK_SET));
 CHECK(fgetc(f)==(700&255)); fclose(f);
 logical_test_reset(); audio_quit(); guicore_quit();
 puts("Raw SD isolation, decoded I/O, memory/file checkpoint rollback and bounds PASS");
 return 0;
}
