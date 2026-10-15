/* Link the AVR core separately, keeping benchmark/test code out of its
 * optimization decisions. No renderer, audio callback, or host controllers. */
#define SDL_MAIN_HANDLED
#undef main
#define main timer1_unused_emulator_main
#include "../../main.c"
#undef main
#include <time.h>

static void benchmark(boole ctc)
{
 cu_state_cpu_t* cpu = cu_avr_get_state();
 clock_t begin;
 unsigned long i;
 main_headless = TRUE;
 memset(cpu, 0, sizeof(*cpu));
 cpu->fuse[1] = 0xD3U;
 cpu->crom[0] = 255U; cpu->crom[1] = 207U; /* RJMP . */
 cu_avr_reset();
 cpu->iors[CU_IO_TCCR1B] = ctc ? 9U : 1U;
 cpu->iors[CU_IO_OCR1AL] = 1819U & 255U;
 cpu->iors[CU_IO_OCR1AH] = 1819U >> 8;
 cpu->iors[CU_IO_OCR1BL] = 909U & 255U;
 cpu->iors[CU_IO_OCR1BH] = 909U >> 8;
 cu_avr_io_update();
 begin = clock();
 for (i = 0; i < 150000UL; ++i){ (void)cu_avr_run(); }
 printf("%s CPU: %.6f s, cycles %u\n", ctc ? "CTC" : "normal", (double)(clock() - begin) / CLOCKS_PER_SEC, (unsigned)cpu->cycle);
}

int main(void){ benchmark(TRUE); benchmark(FALSE); return 0; }
