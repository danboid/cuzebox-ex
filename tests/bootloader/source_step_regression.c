#define SDL_MAIN_HANDLED
#undef main
#define main source_step_emulator_main
#include "../../main.c"
#undef main
/* Build deterministic source intervals around a tiny executable AVR program. */
#include "../../debug_source.c"
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); return 1; } } while (0)

static void prepare(auint pc)
{
 cu_state_cpu_t* cpu = cu_avr_get_state();
 memset(cpu, 0, sizeof(*cpu));
 cpu->crom[2] = 0x02U; cpu->crom[3] = 0xD0U; /* RCALL from 1 to 4 */
 cpu->crom[6] = 0xFFU; cpu->crom[7] = 0xCFU; /* RJMP 3: same-line loop */
 cpu->crom[10] = 0x08U; cpu->crom[11] = 0x95U; /* RET at 5 */
 cpu->fuse[1] = 0xD3U;
 cu_avr_reset(); cpu->pc = pc;
 cu_avr_breakpoints_clear();
 main_ispause = TRUE;
}

static auint run_stop(void)
{
 unsigned n;
 for (n = 0; n < 100U; ++n){ if (cu_avr_run() & CU_BREAK){ return cu_avr_get_state()->pc & 0x7FFFU; } }
 return 0xFFFFFFFFU;
}

int main(void)
{
 cu_state_cpu_t* cpu = cu_avr_get_state();
 appcfg_defaults(&main_cfg);
 main_ispause = TRUE;
 cu_debug_source_reset();
 CHECK(!mainui_debug_step_source(CU_SOURCE_STEP_INTO, 64U), "missing ELF rejected");
 g_rows = calloc(3U, sizeof(*g_rows)); CHECK(g_rows, "allocate source fixture"); g_row_count = 3U;
 g_rows[0] = (cu_debug_source_row_t){0U,2U,0U,10U,1U};
 g_rows[1] = (cu_debug_source_row_t){2U,4U,0U,11U,1U};
 g_rows[2] = (cu_debug_source_row_t){4U,6U,1U,20U,1U};
 prepare(0U);
 CHECK(mainui_debug_step_source(CU_SOURCE_STEP_INTO, 64U) && run_stop() == 4U, "into skips same-line instructions and enters callee file");
 CHECK(!cu_debug_source_step_active && strstr(mainui_debug_get_run_control_status(), "complete"), "completion status");
 prepare(0U);
 CHECK(mainui_debug_step_source(CU_SOURCE_STEP_OVER, 64U) && run_stop() == 2U, "over skips whole called function");
 prepare(0U);
 cpu->crom[0] = 0U; cpu->crom[1] = 0xD0U; /* RCALL .+0: stack allocation */
 cpu->crom[2] = 0x0FU; cpu->crom[3] = 0x90U; /* POP r0 */
 cu_avr_crom_update(0U, 4U);
 CHECK(mainui_debug_step_source(CU_SOURCE_STEP_OVER, 64U) && run_stop() == 2U, "GCC stack-allocation RCALL is not a function call");
 prepare(0U);
 CHECK(mainui_debug_step_source(CU_SOURCE_STEP_INTO, 64U) && run_stop() == 4U, "enter callee for out test");
 main_ispause = TRUE;
 CHECK(mainui_debug_step_source(CU_SOURCE_STEP_OUT, 64U) && run_stop() == 2U, "out returns to caller source line");
 prepare(3U);
 CHECK(mainui_debug_step_source(CU_SOURCE_STEP_INTO, 8U) && run_stop() == 3U, "bounded same-line loop");
 CHECK(strstr(mainui_debug_get_run_control_status(), "limit"), "budget reason reported");
 prepare(0U); cu_avr_breakpoint_set(0U, TRUE);
 CHECK(mainui_debug_step_source(CU_SOURCE_STEP_INTO, 64U) && run_stop() == 4U, "step past current breakpoint once");
 CHECK(cu_avr_breakpoint_get(0U), "current breakpoint preserved");
 prepare(0U); cu_avr_breakpoint_set(4U, TRUE);
 CHECK(mainui_debug_step_source(CU_SOURCE_STEP_OVER, 64U) && run_stop() == 4U && !cu_debug_source_step_active, "callee breakpoint interrupts over");
 prepare(0U);
 CHECK(mainui_debug_step_source(CU_SOURCE_STEP_OVER, 64U), "arm cancellable step");
 mainui_debug_set_paused(TRUE); CHECK(!cu_debug_source_step_active, "manual pause cancels step");
 main_ispause = FALSE;
 CHECK(!mainui_debug_step_source(CU_SOURCE_STEP_INTO, 64U), "running debugger rejects new step");
 prepare(0U);
 CHECK(mainui_debug_step_source(CU_SOURCE_STEP_OVER, 64U), "arm before reset");
 cu_avr_reset(); CHECK(!cu_debug_source_step_active, "reset cancels step");
 /* Verify unmapped paths and interrupt nesting without inventing source rows. */
 CHECK(cu_debug_source_step_begin(0U, CU_SOURCE_STEP_OVER, 64U), "arm IRQ traversal");
 cu_debug_source_step_irq_enter();
 CHECK(!cu_debug_source_step_tick(4U, 0U), "over skips mapped IRQ body");
 CHECK(cu_debug_source_step_tick(2U, 0x33U), "RETI restores caller depth");
 CHECK(cu_debug_source_step_begin(8U, CU_SOURCE_STEP_INTO, 64U), "unmapped start accepted");
 CHECK(!cu_debug_source_step_tick(9U, 0U) && cu_debug_source_step_tick(2U, 0U), "unmapped assembly traversed to source");
 CHECK(cu_debug_source_step_begin(0U, CU_SOURCE_STEP_INTO, 64U), "arm before source reset");
 cu_debug_source_reset(); CHECK(!cu_debug_source_step_active, "symbol reload cancels step");
 (void)cpu;
 puts("Source stepping regression: PASS (real AVR into/over/out, breakpoints, loops, IRQ depth, cancellation)");
 return 0;
}
