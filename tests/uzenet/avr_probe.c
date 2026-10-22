/* Synthetic cartridge: real AVR GPIO and UART, no debugger transport shortcut. */
#include <avr/io.h>
#include <util/delay.h>
#include "un_wire.h"
#include <string.h>
#define RESULT (*(volatile uint8_t *)0x800)
static UnRx rx;
void un_put(uint8_t byte){while(!(UCSR0A&(1<<UDRE0))){} UDR0=byte;}
static void fail(uint8_t code){RESULT=code;for(;;){}}
static unsigned receive(void)
{
 for(unsigned ms=0;ms<2000;ms++){
  while(UCSR0A&(1<<RXC0)){
   uint8_t byte=UDR0;
   ((volatile uint8_t *)0x810)[(*(volatile uint8_t *)0x801)++ & 63]=byte;
   *(volatile uint8_t *)0x802=byte;
   unsigned n=un_receive(&rx,byte);if(n)return n;
  }
  _delay_ms(1);
 }
 fail(0xe0);return 0;
}
int main(void)
{
 RESULT=1;
 DDRD|=8;PORTD|=8; /* release ESP reset */
 UCSR0A=0;UBRR0H=0;UBRR0L=185;UCSR0C=6;
 UCSR0B=0;_delay_ms(1800);UCSR0B=(1<<RXEN0)|(1<<TXEN0);
#ifdef NETWORK_PROBE
 for(unsigned ms=0;ms<10000;ms++){
#else
 for(unsigned ms=0;ms<1000;ms++){
#endif
  while(UCSR0A&(1<<RXC0))(void)UDR0;
  _delay_ms(1);
 }
#ifdef NETWORK_PROBE
 /* The harness releases both cartridges after saved TLS is connected. */
 while(!(*(volatile uint8_t *)0x807)){
  while(UCSR0A&(1<<RXC0))(void)UDR0;
 }
#endif
 RESULT=2;PORTA&=~0x80;DDRA&=~0x80;_delay_ms(150);
 DDRA|=0x80;_delay_ms(30);DDRA&=~0x80;
 RESULT=3;
 for(unsigned ms=0;PINA&0x80;ms++){if(ms==100)fail(0xe1);_delay_ms(1);}
 RESULT=4;
 for(unsigned ms=0;!(PINA&0x80);ms++){if(ms==100)fail(0xe2);_delay_ms(1);}
 DDRA|=0x80;RESULT=5;
 if(receive()!=11 || rx.data[0]!=0x80 || rx.data[3]!=4)fail(0xe3);
 un_send(UN_CAPS,0,0);
 if(receive()!=5 || rx.data[0]!=0x81 || rx.data[1]!=4)fail(0xe4);
 uint8_t info[]={1,12};
 un_send(0x30,info,2);
 if(receive()<3 || rx.data[0]!=0xb1 || rx.data[1]!=1 || rx.data[2]!=0x80)fail(0xe5);
#ifdef NETWORK_PROBE
 uint8_t start[23]={2,1,'C','U','Z','E','T','E','S','T'};
 start[10]=1;start[14]=1;start[18]=1;start[19]=2;start[20]=1;
 un_send(0x30,start,sizeof(start));
 uint8_t tag=2;
 for(unsigned tries=0;;tries++){
  if(receive()<3 || rx.data[0]!=0xb1)fail(0xe8);
  if(rx.data[2]&0x10)fail(0xe9);
  if(rx.data[2]&0x80)break;
  if(tries==200)fail(0xea);
  _delay_ms(20);
  uint8_t poll[]={++tag,2};un_send(0x30,poll,2);
 }
 RESULT=6;
 for(unsigned frame=0;frame<32;frame++){
  for(unsigned tries=0;;tries++){
   uint8_t exchange[]={++tag,3,(uint8_t)(frame+1)};
   un_send(0x30,exchange,3);
   if(receive()<3 || rx.data[0]!=0xb1)fail(0xeb);
   if(rx.data[2]&0x30)fail(0xec);
   if(rx.data[2]&0x80){
    if(rx.data[3]!=frame+1 || rx.data[4]!=frame+1)fail(0xed);
    break;
   }
   if(tries==200)fail(0xee);
   _delay_ms(10);
  }
 }
 RESULT=0x80;for(;;){}
#endif
 un_send(UN_EXIT,0,0);
 if(receive()!=1 || rx.data[0]!=0x83)fail(0xe6);
 _delay_ms(10); /* Keep READY asserted through the firmware's exit worker. */
 DDRA&=~0x80;
 /* Stock AT is restored after the binary EXIT acknowledgement drains. */
 _delay_ms(10);un_put('A');un_put('T');un_put('\r');un_put('\n');
 uint8_t last=0;
 for(unsigned ms=0;ms<2000;ms++){
  while(UCSR0A&(1<<RXC0)){
   uint8_t byte=UDR0;if(last=='O' && byte=='K'){RESULT=0x80;for(;;){}}
   last=byte;
  }
  _delay_ms(1);
 }
 fail(0xe7);
}
