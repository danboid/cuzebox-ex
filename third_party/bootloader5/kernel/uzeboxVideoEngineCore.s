/*
 *  Uzebox Kernel
 *  Copyright (C) 2008-2009 Alec Bourque
 *  Optimized and trimmed to the game by Sandor Zsuga (Jubatian), 2016
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Uzebox is a reserved trade mark
*/

/*
	Changes
	---------------------------------------------
	See http://code.google.com/p/uzebox/source/browse/trunk/README.txt
*/

#include <avr/io.h>
#include "defines.h"



;
; Global assembly delay macro for 0 to 1535 cycles
; Parameters: reg = Registerto use in inner loop (will be destroyed)
;             clocks = CPU clocks to wait
;
.macro WAIT reg, clocks
.if     (\clocks) >= 768
	ldi   \reg,    0
	dec   \reg
	brne  .-4
.endif
.if     ((\clocks) % 768) >= 9
	ldi   \reg,    ((\clocks) % 768) / 3
	dec   \reg
	brne  .-4
.if     ((\clocks) % 3) == 2
	rjmp  .
.elseif ((\clocks) % 3) == 1
	nop
.endif
.elseif ((\clocks) % 768) == 8
	lpm   \reg,    Z
	lpm   \reg,    Z
	rjmp  .
.elseif ((\clocks) % 768) == 7
	lpm   \reg,    Z
	rjmp  .
	rjmp  .
.elseif ((\clocks) % 768) == 6
	lpm   \reg,    Z
	lpm   \reg,    Z
.elseif ((\clocks) % 768) == 5
	lpm   \reg,    Z
	rjmp  .
.elseif ((\clocks) % 768) == 4
	rjmp  .
	rjmp  .
.elseif ((\clocks) % 768) == 3
	lpm   \reg,    Z
.elseif ((\clocks) % 768) == 2
	rjmp  .
.elseif ((\clocks) % 768) == 1
	nop
.endif
.endm



;Public methods
.global TIMER1_COMPA_vect
.global TIMER1_COMPB_vect
.global GetVsyncFlag
.global ClearVsyncFlag
.global WaitVsync
.global SetRenderingParameters
.global SetRenderingParameters_Default
.global ClearRAM
.global ReadJoypad
.global ReadJoypadExt
.global WriteEeprom_Head
.global WriteEeprom
.global ReadEeprom_Head
.global ReadEeprom
.global WaitUs
.global IsRunningInEmulator

;Public variables
.global sync_pulse
.global sync_flags
.global joypad_buf
.global first_render_line_tmp
.global render_lines_count_tmp


;Includes the video mode selected by
;the -DVIDEO_MODE compile switch
#include VMODE_ASM_SOURCE

.section .bss
	.align 1

	; Video frame vars. Order must be preserved for this 6 bytes. They are
	; placed at the end of the RAM to allow for a RAM clear sweep at init
	; excluding them for faster video init. 0x10FF contains the sound
	; sample.

.equ	sync_pulse, RAM_RESERVED + 0 ; Scanline counter
.equ	sync_flags, RAM_RESERVED + 1 ; b0: vsync flag, set at 60Hz when video frame rendered
	                             ; b1: current field (0=odd, 1=even); unused

.equ	first_render_line,      RAM_RESERVED + 2
.equ	render_lines_count,     RAM_RESERVED + 3

.equ	first_render_line_tmp,  RAM_RESERVED + 4
.equ	render_lines_count_tmp, RAM_RESERVED + 5

	; Last read results of joypads

	; Controller shift registers, both ports, 24 bits each (the 8 bits
	; past the standard 16 carry the SNES mouse Y movement). Per port:
	; 3 bytes shifting, 3 bytes latched result.

	joypad_buf:               .space 12

#if TRUE_RANDOM_GEN == 1
	random_value:             .space 2
#endif

.section .text



; General concepts:
;
; This is the bootloader, so aim to be small rather than efficient. For this,
; the whole VSync generation is performed with waits instead of an interrupt
; system.
;
; Since this vsync generator is inherited from Flight of a Dragon, GPIOR1 and
; GPIOR2 are used to replace the stack for pushing & popping some regs.
;
; GPOIR0 is used as follows which may be adapted by later kernels:
;
; bit 0: 68 cycles wide low pulse (normally used by the inline mixer)
; bit 1: 136 cycles wide low pulse (normally used by the inline mixer)
; bit 2: A VSync double rate rising pulse which requires no mixer operation
; bit 3: Within odd field if set (for 262p and 262p+ it is always clear)
; bit 4: "262p+", enable temporal interference cancellation
; bit 5: "525i", enable true interlaced signal generation
;
; Bits 4 and 5 may be stored and restored from EEPROM
;
; Additionally for this bootloader, the followings are defined:
;
; bit 7: Start a "click" sound.



;
; OCR1A Interrupt entry
;
TIMER1_COMPA_vect:

	nop                    ; Compensating for the rjmp entry

	; (3 cy IT entry latency)
	; (3 cy JMP)

	out   _SFR_IO_ADDR(GPIOR1), ZL
	lds   ZL,      _SFR_MEM_ADDR(TCNT1L) ; Timer: 0x08 - 0x0D (5cy jitter)

	sbrc  ZL,      2
	rjmp  .+8              ; 0x0D ( 5) or 0x0C ( 6)
	sbrc  ZL,      1
	rjmp  .+4              ; 0x0B ( 7) or 0x0A ( 8)
	nop
	rjmp  .                ; 0x09 ( 9) or 0x08 (10)
	sbrs  ZL,      0
	rjmp  .

	; An lds of TCNT1L here would result 0x14

	; A fall which requires mixer operation. Depending on required pulse
	; width (68, 136 or 774 cycles) the mixer has to be instructed. There
	; is a bug in the original kernel that the 68 cycle wide pulse is one
	; cycle longer in the mixer, which is compensated by calling it one
	; cycle sooner. This compensation is not present here (so the mixer
	; has to be fixed to use with this kernel). Since the mixer is
	; designed to perform the rise of SYNC, clear the OCR1B interrupt flag
	; here (to discard the rise interrupt request).

	in    ZL,      _SFR_IO_ADDR(SREG)
	out   _SFR_IO_ADDR(GPIOR2), ZL
	push  ZH
	cbi   _SFR_IO_ADDR(SYNC_PORT), SYNC_PIN
	push  r0
	push  r1
	rcall update_sound     ; update_sound is accessible by rcall with proper linking order

	; Normal 136 cycle wide pulses: count them, call video mode where
	; appropriate, transition to vsync on the end running vsync tasks.

	lds   ZL,      sync_pulse
	dec   ZL
	sts   sync_pulse, ZL
	breq  sync_vsync_transfer

	lds   ZH,      first_render_line
	sub   ZL,      ZH
	breq  sync_vmode
	brcc  sync_ctrl

	; Normal lines after vmode

	rjmp  sync_ret



sync_vmode:

	; Enter video mode

	WAIT  ZH,      243 - (AUDIO_OUT_HSYNC_CYCLES)

	; Push registers r2 -> r29

	ldi   ZL,      2
	clr   ZH
sync_vmode_push:
	ld    r0,      Z+
	push  r0
	cpi   ZL,      30
	brne  sync_vmode_push

	call  VMODE_FUNC

	; Note: Video generator should clear the OCR1A and OCR1B interrupt
	; flags before returning.

	; Pop registers and be done with it

	ldi   ZL,      30
	clr   ZH
sync_vmode_pop:
	dec   ZL
	pop   r0
	st    Z,       r0
	brne  sync_vmode_pop
	rjmp  sync_vmode_ret



sync_vsync_transfer:

;      +--> We are here (after sound output)
;      |
;  1820=>0
; _|0~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|1684_|1752~
; ~~~~~~~~~~~~~~~~~~|774__|842~~~~~~~~~~~~~~~~~~|1684_|1752~
; ~~~~~~~~~~~~~~~~~~|774__|842~~~~~~~~~~~~~~~~~~|1684_|1752~
; ~~~~~~~~~~~~~~~~~~|774__|842~~~~~~~~~~~~~~~~~~|1684_______
; ____________|638~~|774__________________|1548~|1684_______
; ____________|638~~|774__________________|1548~|1684_______
; ____________|638~~|774__________________|1548~|1684_|1752~
; ~~~~~~~~~~~~~~~~~~|774__|842~~~~~~~~~~~~~~~~~~|1684_|1752~
; ~~~~~~~~~~~~~~~~~~|774__|842~~~~~~~~~~~~~~~~~~|1684_|1752~
; ~~~~~~~~~~~~~~~~~~|774__|842~~~~~~~~~~~~~~~~~~|1684_______
;                                                |
;                        Mixer is called here <--+
;                        (Also return here after last call)

	WAIT  ZL,      1026 - (AUDIO_OUT_HSYNC_CYCLES)
	WAIT  ZL,      768
	rcall sync_vsync_mix_68c
	rcall sync_vsync_mix_68c
	rcall sync_vsync_mix_68c
	rcall sync_vsync_mix_inv
	rcall sync_vsync_mix_inv
	rcall sync_vsync_mix_inv
	rcall sync_vsync_mix_68c
	rcall sync_vsync_mix_68c
	rcall sync_vsync_mix_68c
	lpm   ZL,      Z
	sbi   _SFR_IO_ADDR(GPIOR0), 1 ; 136 cycles wide pulses enabled
	cbi   _SFR_IO_ADDR(GPIOR0), 0 ; 68 cycles wide pulses disabled
	rcall sync_vsync_mix

	; Keep render height registers updated

	ldi   ZL,      (SYNC_HSYNC_PULSES - 1)
	mov   r0,      ZL
	ldi   ZL,      lo8(sync_pulse)
	ldi   ZH,      hi8(sync_pulse)
	std   Z + 0,   r0      ; sync_pulse
	ldd   r1,      Z + 4   ; first_render_line_tmp
	sub   r0,      r1
	std   Z + 2,   r0      ; first_render_line
	ldd   r0,      Z + 5   ; render_lines_count_tmp
	std   Z + 3,   r0      ; render_lines_count

	; Clear any pending timer int

	ldi   ZL,      (1 << OCF1A)
	sts   _SFR_MEM_ADDR(TIFR1), ZL

	; Done

	rjmp  sync_ret



sync_ctrl:

	; Lines before display: Read the controllers
	; ZL contains lines until transfer to video (0), which are used the
	; following manner:
	; 26+: Raise controller latch pin
	; 25: Release controller latch pin
	; 1-24: Read controller data bits (both ports, 24 bits for SNES mouse)
	; The clock is normally kept low. Rising edges advance the shifer in
	; the controller.

	cpi   ZL,      25
	brcs  sync_ctrl_rd

	sbi   _SFR_IO_ADDR(JOYPAD_OUT_PORT), JOYPAD_LATCH_PIN
	brne  .+2
	cbi   _SFR_IO_ADDR(JOYPAD_OUT_PORT), JOYPAD_LATCH_PIN
	rjmp  sync_ret

sync_ctrl_rd:

	; T set on the last bit: latch the shifted results. Both data pins
	; are sampled at once (PA0: P1, PA1: P2; low = 1 bit).

	cbi   _SFR_IO_ADDR(JOYPAD_OUT_PORT), JOYPAD_CLOCK_PIN
	clt
	cpi   ZL,      1
	brne  .+2
	set
	in    r1,      _SFR_IO_ADDR(JOYPAD_IN_PORT)
	com   r1
	ldi   ZL,      lo8(joypad_buf)
	ldi   ZH,      hi8(joypad_buf)
sync_ctrl_port:
	lsr   r1               ; Carry: this port's data bit
	ldd   r0,      Z + 2
	ror   r0
	std   Z + 2,   r0
	brtc  .+2
	std   Z + 5,   r0
	ldd   r0,      Z + 1
	ror   r0
	std   Z + 1,   r0
	brtc  .+2
	std   Z + 4,   r0
	ld    r0,      Z
	ror   r0
	st    Z,       r0
	brtc  .+2
	std   Z + 3,   r0
	adiw  ZL,      6
	cpi   ZL,      lo8(joypad_buf + 12)
	brne  sync_ctrl_port
	sbi   _SFR_IO_ADDR(JOYPAD_OUT_PORT), JOYPAD_CLOCK_PIN

sync_ret:

	pop   r1
	pop   r0

sync_vmode_ret:

	pop   ZH
	in    ZL,      _SFR_IO_ADDR(GPIOR2)
	out   _SFR_IO_ADDR(SREG), ZL
	in    ZL,      _SFR_IO_ADDR(GPIOR1)
	reti



sync_vsync_mix:
	cbi   _SFR_IO_ADDR(SYNC_PORT), SYNC_PIN
	lpm   ZL,      Z
	rjmp  .
	rjmp  update_sound     ; update_sound is accessible by rcall with proper linking order

sync_vsync_mix_68c:
	cbi   _SFR_IO_ADDR(GPIOR0), 1 ; 136 cycles wide pulses disabled
	sbi   _SFR_IO_ADDR(GPIOR0), 0 ; 68 cycles wide pulses enabled
	rcall sync_vsync_mix
	WAIT  ZL,      901 - (AUDIO_OUT_HSYNC_CYCLES)
	cbi   _SFR_IO_ADDR(SYNC_PORT), SYNC_PIN
	WAIT  ZL,      66
	sbi   _SFR_IO_ADDR(SYNC_PORT), SYNC_PIN
	WAIT  ZL,      826
	ret

sync_vsync_mix_inv:
	cbi   _SFR_IO_ADDR(GPIOR0), 1 ; 136 cycles wide pulses disabled
	cbi   _SFR_IO_ADDR(GPIOR0), 0 ; 68 cycles wide pulses disabled
	rcall sync_vsync_mix
	WAIT  ZL,      766 - (AUDIO_OUT_HSYNC_CYCLES)
	sbi   _SFR_IO_ADDR(SYNC_PORT), SYNC_PIN
	WAIT  ZL,      134
	cbi   _SFR_IO_ADDR(SYNC_PORT), SYNC_PIN
	WAIT  ZL,      772
	sbi   _SFR_IO_ADDR(SYNC_PORT), SYNC_PIN
	WAIT  ZL,      120
	ret



;
; Generate a H-Sync pulse
;
; For use by video modes when rendering a frame
;
; Clobbers: r0, r1, ZL, ZH, T flag
;
hsync_pulse:
	cbi   _SFR_IO_ADDR(SYNC_PORT), SYNC_PIN
	rjmp  .
	rjmp  .
	rcall update_sound
	lds   ZL,      sync_pulse
	dec   ZL
	sts   sync_pulse, ZL
	ret



;************************************
; This flag is set on each VSYNC by
; the engine. This func is used to
; synchronize the programs on frame
; rate (60hz).
;
; C-callable
;************************************
.section .text.GetVsyncFlag
GetVsyncFlag:
	lds r24,sync_flags
	andi r24,SYNC_FLAG_VSYNC
	ret

;*****************************
; Clear the VSYNC flag.
;
; C-callable
;*****************************
.section .text.ClearVsyncFlag
ClearVsyncFlag:
	lds r18,sync_flags
	andi r18,~SYNC_FLAG_VSYNC
	sts sync_flags,r18
	ret


;*****************************
; Waits VSYNCs
; C-callable
;     r24: Count of VSYNCs to wait
;*****************************
.section .text.WaitVsync
WaitVsync:
	lds   r25,     sync_flags
	mov   r23,     r25
	andi  r23,     SYNC_FLAG_VSYNC
	breq  WaitVsync
	andi  r25,     (0xFF ^ SYNC_FLAG_VSYNC)
	sts   sync_flags, r25
	dec   r24
	brne  WaitVsync
	ret


;*****************************
; Set rendering bounds
; C-callable
;     r24: First scanline to render
;     r22: Scan lines to render
;*****************************
.section .text.SetRenderingParameters
SetRenderingParameters_Default:
	ldi   r24,     FIRST_RENDER_LINE
	ldi   r22,     FRAME_LINES
SetRenderingParameters:
	sts   first_render_line_tmp, r24
	sts   render_lines_count_tmp, r22
	ret


;*****************************
; Clears a memory region
; C-callable
; r25:r24: Start of memory region
; r23:r22: Length of memory region
;*****************************
.section .text.ClearRAM
ClearRAM:
	add   r22,     r24
	adc   r23,     r25
	movw  XL,      r24
	clr   r1
ClearRAM_l:
	st    X+,      r1
	cp    XL,      r22
	cpc   XH,      r23
	brne  ClearRAM_l
	ret


;*****************************
; Return joypad 1 or 2 buttons status
; C-callable
; r24=joypad No (0 or 1)
; returns: (int) r25:r24
;*****************************
.section .text.ReadJoypad
ReadJoypad:

	; Returns the buttons of both ports merged (either port may hold a
	; controller or a SNES mouse). For a mouse, the signature and
	; sensitivity bits are dropped, the buttons stay (Right: BTN_A,
	; Left: BTN_X), and any vertical movement reads as BTN_UP / BTN_DOWN.
	; Clobbers: r21 - r23, ZL, ZH

	clr   r24
	clr   r25
	ldi   ZL,      lo8(joypad_buf + 3)
	ldi   ZH,      hi8(joypad_buf + 3)
	rcall rj_port
	adiw  ZL,      3
rj_port:
	ld    r22,     Z+
	ld    r23,     Z+
	ld    r21,     Z+      ; bit 0: Y direction (1: up), 1-7: magnitude
	sbrs  r23,     7       ; Mouse signature (bit 15)
	rjmp  rj_merge
	andi  r23,     (BTN_A | BTN_X) >> 8
	cpi   r21,     2       ; Any magnitude (bits 1-7)?
	brcs  rj_merge
	ldi   r22,     BTN_DOWN
	sbrc  r21,     0
	ldi   r22,     BTN_UP
rj_merge:
	or    r24,     r22
	or    r25,     r23
	ret


;****************************
; Wait for n microseconds
; r25:r24 - us to wait
; returns: void
;****************************
.section .text.WaitUs
WaitUs:
	ldi r23,8
	dec 23
	brne .-4 ;~1 us
	nop
	sbiw r24,1
	brne WaitUs

	ret


;****************************
; Write byte to EEPROM
; extern void WriteEeprom(int addr,u8 value)
; r25:r24 - addr
; r22 - value to write
;****************************
.section .text.WriteEeprom
WriteEeprom_Head:

	ldi   r25,     0       ; Write to EEPROM's beginning

WriteEeprom:

	; Check current byte at location (don't write if already OK)
	; This also waits previous write's end, so no need to check.

	movw  r20,     r24
	rcall ReadEeprom
	cp    r24,     r22
	breq  WriteEeprom_ret

	; Set up address (r21:r20) in address register
	out   _SFR_IO_ADDR(EEARH), r21
	out   _SFR_IO_ADDR(EEARL), r20
	; Write data (r22) to Data Register
	out   _SFR_IO_ADDR(EEDR), r22
	cli
	; Write logical one to EEMPE
	sbi   _SFR_IO_ADDR(EECR), EEMPE
	; Start eeprom write by setting EEPE
	sbi   _SFR_IO_ADDR(EECR), EEPE
	sei

WriteEeprom_ret:

	ret


;****************************
; Read byte from EEPROM
; extern unsigned char ReadEeprom(int addr)
; r25:r24 - addr
; r24 - value read
;****************************
.section .text.ReadEeprom
ReadEeprom_Head:

	ldi   r25,     0       ; Read from EEPROM's beginning

ReadEeprom:

	; Wait for completion of previous write
	sbic  _SFR_IO_ADDR(EECR), EEPE
	rjmp  ReadEeprom
	; Set up address (r25:r24) in address register
	out   _SFR_IO_ADDR(EEARH), r25
	out   _SFR_IO_ADDR(EEARL), r24
	; Start eeprom read by writing EERE
	cli
	sbi   _SFR_IO_ADDR(EECR), EERE
	; Read data from Data Register
	in    r24,     _SFR_IO_ADDR(EEDR)
	sei
	ret


;****************************
; Wait for the specified amount of clocks * 4
; Note this is approximative.
; C callable
; r25:r24 - clocks to wait
;****************************
.global WaitClocks
.section .text.WaitClocks
WaitClocks:

1:
	sbiw r24,1
	brne 1b

	ret

;****************************
; Turns on the onboard LED on PD4
; C callable
;****************************
.global SetLedOn
.section .text.SetLedOn
SetLedOn:
	sbi _SFR_IO_ADDR(PORTD),PD4
	ret

;****************************
; Turns off the onboard LED on PD4
; C callable
;****************************
.global SetLedOff
.section .text.SetLedOff
SetLedOff:
	cbi _SFR_IO_ADDR(PORTD),PD4
	ret

;****************************
; Toggles the onboard LED on PD4
; C callable
;****************************
.global ToggleLed
.section .text.ToggleLed
ToggleLed:
	sbi _SFR_IO_ADDR(PIND),PD4
	ret
