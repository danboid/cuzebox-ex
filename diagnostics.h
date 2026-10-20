/* Optional host-side diagnostics. Never part of emulated machine state. */
#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H
#include "types.h"
enum {
 DIAG_SPI_COLLISION=0, DIAG_SPI_UNREAD, DIAG_UART_OVERRUN,
 DIAG_STACK_IRQ, DIAG_SYNC, DIAG_WATCHDOG, DIAG_GUARD,
 DIAG_REPLAY, DIAG_ASSERT, DIAG_KIND_COUNT
};
#ifdef ENABLE_DEBUGGER
extern boole diagnostics_gate;
extern boole diagnostics_recording;
void diagnostics_enable(boole enable);
boole diagnostics_enabled(void);
void diagnostics_event(unsigned kind, unsigned detail);
void diagnostics_instruction(unsigned pc);
void diagnostics_spi_complete(void);
void diagnostics_spi_read(void);
void diagnostics_sync(unsigned cycle);
void diagnostics_input(unsigned port,unsigned slot,uint64_t packet,unsigned bits);
void diagnostics_machine_changed(void);
void diagnostics_peripheral_reset(void);
void diagnostics_service(void);
boole diagnostics_pending(void);
boole diagnostics_arm(char const *directory);
void diagnostics_disarm(void);
char const *diagnostics_status(void);
char const *diagnostics_last_warning(void);
void diagnostics_command(char *args,char *reply,unsigned capacity);
#define DIAGNOSTIC(kind,detail) do { if(diagnostics_gate) diagnostics_event(kind,detail); } while(0)
#define DIAG_CALL(call) do { if(diagnostics_gate) { call; } } while(0)
#else
#define DIAGNOSTIC(kind,detail) ((void)0)
#define DIAG_CALL(call) ((void)0)
#endif
#endif
