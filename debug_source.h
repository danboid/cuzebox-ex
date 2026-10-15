#ifndef DEBUG_SOURCE_H
#define DEBUG_SOURCE_H

#include <stdint.h>

typedef struct{
	uint32_t file_index;
	uint32_t line;
	uint32_t column;
	uint32_t word_addr;
	uint32_t end_word_addr;
} cu_debug_source_location_t;

void        cu_debug_source_reset(void);
int         cu_debug_source_load_elf(char const* path);
char const* cu_debug_source_status(void);
uint32_t    cu_debug_source_file_count(void);
uint32_t    cu_debug_source_row_count(void);
char const* cu_debug_source_file_path(uint32_t index);
int         cu_debug_source_lookup(uint32_t word_addr, cu_debug_source_location_t* out);
int         cu_debug_source_resolve_line(uint32_t file_index, uint32_t line, cu_debug_source_location_t* out);

#define CU_SOURCE_STEP_INTO 1U
#define CU_SOURCE_STEP_OVER 2U
#define CU_SOURCE_STEP_OUT  3U
/* Inactive stepping costs only a gate check in debugger builds. Depth tracks
 * actual CALL/RET and interrupt entry/RETI, not pushes of local variables. */
extern int cu_debug_source_step_active;
extern int cu_debug_source_step_first;
int         cu_debug_source_step_begin(uint32_t pc, unsigned mode, uint32_t limit);
void        cu_debug_source_step_clear(void);
void        cu_debug_source_step_irq_enter(void);
int         cu_debug_source_step_tick(uint32_t next_pc, unsigned opcode_id);
char const* cu_debug_source_step_status(void);

#endif
