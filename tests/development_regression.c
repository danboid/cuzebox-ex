#define SDL_MAIN_HANDLED
#undef main
#define main development_emulator_main
#include "../main.c"
#undef main
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"Development regression failed at %d: %s\n",__LINE__,#x); return 1; } } while(0)

static void spir_command(unsigned op,unsigned address)
{
 cu_spir_cs_set(FALSE,0); cu_spir_cs_set(TRUE,0);
 cu_spir_send(op,1); cu_spir_send(address>>16,2);
 cu_spir_send((address>>8)&255,3); cu_spir_send(address&255,4);
}
int main(void)
{
 unsigned slot,region,flags,addr,value,pc,cycle,row,beam;
 cu_avr_reset(); cu_spir_set_size(2);
 cu_avr_watchpoints_clear(); cu_avr_memory_trace_enable(TRUE);
 cu_avr_memory_trace_clear();
 cu_avr_watchpoint_set(0,TRUE,CU_AVR_WATCH_REGION_SPIRAM,CU_AVR_WATCH_WRITE,0x10001,0x10001);
 spir_command(2,0x10001); cu_spir_send(0x5a,123);
 CHECK(cu_avr_watchpoint_get_last(FALSE,&slot,&region,&flags,&addr,&value,&pc));
 CHECK(region==CU_AVR_WATCH_REGION_SPIRAM && addr==0x10001 && value==0x5a);
 cu_avr_watchpoint_timing(&cycle,&row,&beam); CHECK(cycle==123);
 cu_avr_memtrace_event_t event;
 CHECK(cu_avr_memory_trace_get(cu_avr_memory_trace_first_seq(),&event));
 CHECK(event.addr==0x10001 && event.abs_cycle==123);
 cu_avr_watchpoints_clear();
 cu_avr_watchpoint_set(1,TRUE,CU_AVR_WATCH_REGION_SPIRAM,CU_AVR_WATCH_READ|CU_AVR_WATCH_OUTSIDE,0x10000,0x10002);
 spir_command(3,0x10001);
 CHECK(cu_spir_recv(5)==0x5a);
 CHECK(!cu_avr_watchpoint_get_last(FALSE,NULL,NULL,NULL,NULL,NULL,NULL));
 cu_spir_send(0,6); /* Prefetch 0x10002, still inside the guard. */
 CHECK(!cu_avr_watchpoint_get_last(FALSE,NULL,NULL,NULL,NULL,NULL,NULL));
 cu_spir_send(0,7);
 CHECK(cu_avr_watchpoint_get_last(TRUE,&slot,&region,&flags,&addr,&value,&pc) && addr==0x10003);
 cu_avr_watchpoints_clear(); cu_avr_memory_trace_enable(FALSE);
 unsigned count=cu_avr_memory_trace_count();
 spir_command(2,0x1ffff); cu_spir_send(0x34,8); cu_spir_send(0x56,9);
 CHECK(cu_spir_get_state()->ram[0x1ffff]==0x34 && cu_spir_get_state()->ram[0]==0x56);
 CHECK(cu_avr_memory_trace_count()==count);
 cu_spir_deinit();
 puts("Decoded SPI watch/guard: high addresses, read/write, timing, wrap and disabled tracing PASS");
 return 0;
}
