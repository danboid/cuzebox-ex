/* ATmega644 master SPI register semantics (datasheet sections 16.2, 16.5).
 * Byte-level peripheral transport remains in cu_spi.c. */
#ifndef CU_AVR_SPI_H
#define CU_AVR_SPI_H
#include "cu_types.h"

static inline auint cu_avr_spi_status(cu_state_cpu_t *s)
{
 s->spi_clear = s->iors[CU_IO_SPSR] & 0xC0U;
 return s->iors[CU_IO_SPSR];
}

static inline void cu_avr_spi_access(cu_state_cpu_t *s)
{
 s->iors[CU_IO_SPSR] &= ~s->spi_clear;
 s->spi_clear = 0U;
}

static inline boole cu_avr_spi_start(cu_state_cpu_t *s, auint data)
{
 /* Reading SPSR then either reading OR writing SPDR clears observed flags.
  * A plain write must not clear a completion from an earlier transfer. */
 static const auint divisors[4] = {4U, 16U, 64U, 128U};
 auint clocks;
 cu_avr_spi_access(s);
 if (!(s->iors[CU_IO_SPCR] & 0x40U)) return FALSE;
 if (s->spi_tran){
  s->iors[CU_IO_SPSR] |= 0x40U;
  return FALSE; /* Collision does not replace the active byte or its deadline. */
 }
 clocks = divisors[s->iors[CU_IO_SPCR] & 3U] * 8U;
 if (s->iors[CU_IO_SPSR] & 1U) clocks /= 2U;
 s->spi_tran = TRUE;
 s->spi_tx = data;
 s->spi_end = WRAP32(s->cycle + clocks);
 return TRUE;
}

static inline void cu_avr_spi_complete(cu_state_cpu_t *s)
{
 s->spi_tran = FALSE;
 s->iors[CU_IO_SPSR] |= 0x80U;
 s->iors[CU_IO_SPDR] = s->spi_rx;
}
#endif
