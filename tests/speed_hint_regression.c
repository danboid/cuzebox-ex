#define SDL_MAIN_HANDLED
#undef main
#define main speed_emulator_main
#include "../main.c"
#undef main
#include <assert.h>

static void word(unsigned address, unsigned value)
{
 cu_state_cpu_t* s = cu_avr_get_state();
 s->crom[address*2] = value;
 s->crom[address*2+1] = value >> 8;
}
static void marker(unsigned command)
{
 word(0,0xc004); word(1,0x4355); word(2,0x4558);
 word(3,command); word(4,command ^ 0xffff);
 cu_avr_crom_update(0,10);
}
static void execute(void)
{
 cu_state_cpu_t* s = cu_avr_get_state();
 unsigned cycle = s->cycle;
 uint8 ram[sizeof(s->sram)];
 uint8 regs[32], flags = s->iors[CU_IO_SREG];
 memcpy(regs,s->iors,32);
 memcpy(ram,s->sram,sizeof(ram));
 s->pc = 0;
 cu_avr_debug_step_instructions(1);
 cu_avr_run();
 assert(s->pc == 5 && s->cycle == cycle+2);
 assert(!memcmp(ram,s->sram,sizeof(ram)));
 assert(!memcmp(regs,s->iors,32) && flags == s->iors[CU_IO_SREG]);
}
int main(void)
{
 cu_state_cpu_t* s = cu_avr_get_state();
 unsigned i;
 cu_avr_reset();
 s->iors[CU_IO_SREG] = 0x35;
 memset(s->iors,0xa5,32);
 marker(1); execute(); assert(cu_avr_speed_requested());
 cu_avr_flash_active(TRUE); assert(cu_avr_speed_requested());
 marker(0); execute(); assert(!cu_avr_speed_requested());
 for(i=2;i<10;i++){
  marker(1); s->crom[i] ^= 0x80;
  cu_avr_crom_update(0,10); execute(); assert(!cu_avr_speed_requested());
 }
 marker(2); execute(); assert(!cu_avr_speed_requested());
 marker(1); execute(); assert(cu_avr_speed_requested());
 cu_avr_io_update(); assert(!cu_avr_speed_requested());
 marker(1); execute(); assert(cu_avr_speed_requested());
 cu_avr_reset(); assert(!cu_avr_speed_requested());
 marker(1); execute();
 main_cfg.fast_flash = FALSE;
 main_headless = TRUE;
 main_ispause = FALSE; main_isadvfr = FALSE;
 main_nolimit = FALSE;
 main_acceleration_tick(1); assert(main_acceleration_active && !main_nolimit);
 main_ispause = TRUE;
 main_acceleration_tick(2); assert(!main_acceleration_active);
 main_ispause = FALSE; main_np_session_active = TRUE;
 main_acceleration_tick(3); assert(!main_acceleration_active);
 main_np_session_active = FALSE;
 main_acceleration_tick(4); assert(main_acceleration_active);
 marker(0); execute(); main_acceleration_tick(5);
 assert(!main_acceleration_active && !main_nolimit);
 main_nolimit = TRUE; main_acceleration_tick(6); assert(main_nolimit);
 puts("PASS: speed hints, malformed markers, AVR timing/registers, reset/restore, pause/netplay, user pacing");
 return 0;
}
