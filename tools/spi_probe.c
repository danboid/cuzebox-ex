/* AVR-level regression: exercise real IN/OUT register dispatch in CUzeBox. */
#include <avr/io.h>
#include <util/delay_basic.h>
volatile unsigned char probe_result;
__attribute__((noinline,noreturn)) void probe_done(void){for(;;)asm volatile("nop");}
#define CHECK(c,n) do{if(!(c)){probe_result=(n);probe_done();}}while(0)
static void finish(void){_delay_loop_2(16);}
int main(void){
 PORTA|=_BV(PA4);DDRA|=_BV(PA4);PORTD|=_BV(PD6);DDRD|=_BV(PD6);
 DDRB|=_BV(PB4)|_BV(PB5)|_BV(PB7);
 SPCR=_BV(SPE)|_BV(MSTR);SPSR=_BV(SPI2X);
 SPDR=0x31;finish();
 (void)SPDR; /* SPDR access alone must leave SPIF set. */
 CHECK(SPSR&_BV(SPIF),1);
 (void)SPDR;CHECK(!(SPSR&_BV(SPIF)),2);
 SPDR=0x32;finish();
 SPDR=0x33; /* No preceding SPSR read: old completion must survive. */
 CHECK(SPSR&_BV(SPIF),3);
 finish();(void)SPSR;(void)SPDR;
 asm volatile("out %0,%1\n\tout %0,%1"::"I"(_SFR_IO_ADDR(SPDR)),"r"((unsigned char)0x55));
 CHECK(SPSR&_BV(WCOL),4);
 (void)SPDR;CHECK(!(SPSR&_BV(WCOL)),5);
 finish();SPSR=0; /* SPIF is read-only, not write-zero-to-clear. */
 CHECK(SPSR&_BV(SPIF),6);(void)SPDR;
 SPSR=0xFE;CHECK(!(SPSR&0xFE),7); /* Can't set flags or reserved bits. */
 probe_result=0xA5;probe_done();
}
